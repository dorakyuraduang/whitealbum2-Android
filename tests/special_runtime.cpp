#include "runtime.hpp"
#include "../port/guest_api.hpp"
#include "../platform/window/gui.hpp"
#include "../platform/compat/legacy_display.hpp"
#include <winrecomp/win32_state.hpp>
#include <fstream>
#include <iostream>
#include <set>
#include <random>
#include "../recompiled/special/lzss.hpp"
#include "../platform/compat/io_diagnostics.hpp"

#define CHECK(x) do{if(!(x))throw std::runtime_error("check failed: " #x);}while(false)
using wr::U32;
class MemoryHost final:public wa2::window::Host {
public:
    unsigned devices{},windows{},frames{};
    SDL_Window* acquire(U32,U32,U32,bool)override{++devices;throw std::runtime_error("memory fixture cannot acquire a display");}
    bool embedded_gl()const noexcept override{return false;}
    void frame_submitted()override{++frames;throw std::runtime_error("memory fixture cannot present");}
    void create(const wa2::window::WindowSpec&)override{++windows;throw std::runtime_error("memory fixture cannot create windows");}
    void destroy(U32)override{}void show(U32,bool)override{}void title(U32,std::u16string_view)override{}
    void position(U32,const wa2::window::Bounds&,U32)override{}
    wa2::window::Bounds client(U32)const override{return {0,0,1280,720};}
    wa2::window::Bounds desktop()const override{return {0,0,1280,720};}
    bool visible(U32)const override{return false;}bool focused(U32)const override{return false;}
    void focus(U32)override{}std::array<std::int32_t,2> cursor()const override{return {};}
    void cursor(std::int32_t,std::int32_t)override{}int show_cursor(bool)override{return 0;}
    std::uint16_t key(U32)override{return 0;}
    std::vector<wa2::window::Event> events(unsigned)override{return {};}
    U32 message_box(std::u16string_view,std::u16string_view,U32)override{throw std::runtime_error("memory fixture has no dialogs");}
    void create_dialog(const wa2::window::DialogState&)override{throw std::runtime_error("memory fixture has no dialogs");}
    void update_dialog(const wa2::window::DialogState&)override{}
    void menu(U32,const wa2::window::MenuTemplate&)override{}
};
class MemoryFiles final:public wr::MutableFiles {
public:
    std::string opened,listed,changed;
    unsigned queries{},identities{},opens{},removes{},moves{};
    std::set<std::string> directories{""};
    std::optional<wr::ReadOnlyFileInfo> info(std::string_view name)override{
        ++queries;
        if(directories.contains(std::string(name)))return wr::ReadOnlyFileInfo{std::string(name),0,0,16};
        if(name.ends_with("Sys.sav") || name.ends_with("sys.sav"))return wr::ReadOnlyFileInfo{"Sys.sav",4,0,128};
        return {};
    }
    std::string identity(std::string_view name)override{++identities;return std::string(name);}
    std::vector<wr::ReadOnlyFileInfo> list(std::string_view name)override{listed=name;return {{"Sys.sav",4,0,128},{"save_00.sav",4,0,128},{"bad.pak",4,0,128}};}
    std::FILE* open(std::string_view name,bool,bool write,U32,U32& error)override{
        ++opens;opened=name;if(write)changed=name;error=0;auto* file=std::tmpfile();if(file){std::fwrite("TEST",1,4,file);std::rewind(file);}else error=5;return file;
    }
    U32 directory(std::string_view name)override{return directories.emplace(name).second?0:183;}
    U32 remove(std::string_view name,bool)override{++removes;changed=name;return 0;}
    U32 move(std::string_view,std::string_view name)override{++moves;changed=name;return 0;}
    U32 attributes(std::string_view name,U32)override{changed=name;return 0;}
    void replace(std::string_view name,std::span<const std::uint8_t>)override{changed=name;}
};
unsigned storage_probe_regression(wr::Process& p,const MemoryFiles& files){
    const auto call=[&](const char* api,std::initializer_list<U32> args){return p.callback(p.resolve("kernel32.dll",api),std::span(args.begin(),args.size()));};
    const auto activity=[&]{return std::array{files.queries,files.identities,files.opens,files.removes,files.moves};};
    const std::array names{"optional.bin","save_100.sav","other/Sys.sav","Documents/Leaf/Other/Sys.sav"};
    unsigned cases{};
    for(const auto* name:names){
        const auto before=activity();const auto ansi=p.put_string(name);
        const auto wide=p.put_wstring(std::u16string(name,name+std::char_traits<char>::length(name)));
        CHECK(call("CreateFileA",{ansi,0x80000000,7,0,3,0x80,0})==0xffffffffu && p.last_error==2);++cases;
        CHECK(call("CreateFileW",{wide,0x80000000,7,0,3,0x80,0})==0xffffffffu && p.last_error==2);++cases;
        for(const auto creation:{1u,2u,4u}){
            CHECK(call("CreateFileA",{ansi,0x40000000,7,0,creation,0x80,0})==0xffffffffu && p.last_error==5);++cases;
        }
        CHECK(call("GetFileAttributesA",{ansi})==0xffffffffu && p.last_error==2);++cases;
        CHECK(activity()==before);
    }
    // Live handles force Delete/Move through the sharing-key lookup too.
    const auto save=p.put_string("Sys.sav"),handle=call("CreateFileA",{save,0x80000000,1,0,3,0x80,0});
    CHECK(handle!=0xffffffffu);++cases;
    const auto other=p.put_string("another-optional.bin");
    for(const auto* name:names){
        const auto before=activity();const auto ansi=p.put_string(name);
        CHECK(call("DeleteFileA",{ansi})==0 && p.last_error==5);++cases;
        CHECK(call("MoveFileA",{ansi,other})==0 && p.last_error==5);++cases;
        CHECK(activity()==before);
    }
    const auto alias=p.put_string("DOCUMENTS\\LEAF\\WHITE ALBUM2 SPECIAL CONTENTS\\SYS.SAV");
    const auto opens=files.opens;
    CHECK(call("CreateFileA",{alias,0x40000000,7,0,3,0x80,0})==0xffffffffu && p.last_error==32);++cases;
    CHECK(files.opens==opens && call("CloseHandle",{handle})==1);
    return cases;
}
unsigned lzss_regression(wr::Process& p,unsigned& declines){
    constexpr U32 ring=0xa79d10,size=65536;
    const auto source=p.allocate_bytes(size),target=p.allocate_bytes(size),stack=p.cpu.r[wr::ESP]-2048;
    std::vector<std::uint8_t> initial_stack(2048),reference_stack(2048),actual_stack(2048),
        blank(size,0xa5),reference_output(size),actual_output(size);
    std::array<std::uint8_t,4096> initial_ring,reference_ring,actual_ring;
    p.memory.copy_out(stack,initial_stack);const auto fp=p.cpu.fp;
    std::mt19937 random(0x4c5a5353);unsigned cases{};
    std::vector<unsigned> token_counts;for(unsigned n=0;n<96;++n)token_counts.push_back(n);
    for(const auto n:{512u,1024u,2048u,4096u})token_counts.push_back(n);
    for(const auto tokens:token_counts){
        std::vector<std::uint8_t> encoded;unsigned output{};
        for(unsigned token=0;token<tokens;){
            const auto flag=encoded.size();encoded.push_back(0);
            for(unsigned bit=0;bit<8 && token<tokens;++bit,++token){
                if(tokens%3==0 || random()%3==0){encoded[flag]|=std::uint8_t(1u<<bit);encoded.push_back(std::uint8_t(random()));++output;}
                else{const auto offset=random()%4096,length=3+random()%16;
                    encoded.push_back(std::uint8_t(offset));encoded.push_back(std::uint8_t((offset>>4&0xf0)|(length-3)));output+=length;}
            }
        }
        for(unsigned n=0;n<initial_ring.size();++n)initial_ring[n]=std::uint8_t(n*7+tokens);
        CHECK(encoded.size()<=size && output<=size);
        const auto reset=[&]{p.memory.copy_in(source,encoded);p.memory.copy_in(target,blank);
            p.memory.copy_in(ring,initial_ring);p.memory.copy_in(stack,initial_stack);p.cpu.fp=fp;};
        reset();U32 reference{};
        {wa2::special::LzssReferenceScope original;reference=p.callback(0x438270,std::array<U32,4>{target,source,U32(encoded.size()),output},false);}
        CHECK(reference==output);p.memory.copy_out(target,reference_output);p.memory.copy_out(ring,reference_ring);p.memory.copy_out(stack,reference_stack);
        const auto reference_fp=p.cpu.fp;reset();const auto before=wa2::special::lzss_activity();
        CHECK(p.callback(0x438270,std::array<U32,4>{target,source,U32(encoded.size()),output},false)==reference);
        CHECK(wa2::special::lzss_activity()==before+1);
        p.memory.copy_out(target,actual_output);p.memory.copy_out(ring,actual_ring);p.memory.copy_out(stack,actual_stack);
        CHECK(actual_output==reference_output && actual_ring==reference_ring && actual_stack==reference_stack && p.cpu.fp.bytes==reference_fp.bytes);++cases;
    }
    const auto local=p.allocate_bytes(4096),frame=local+128;
    for(unsigned reason=0;reason<8;++reason){
        p.memory.copy_in(source,std::array<std::uint8_t,2>{1,'Q'});
        for(const auto delta:{4u,8u,12u,16u})p.memory.store(frame-delta,0,32);
        p.memory.store(frame+16,2,32);p.memory.store(frame+20,1,32);
        wr::Cpu cpu;cpu.eip=0x4382a6;cpu.r[wr::EBP]=frame;cpu.r[wr::ESI]=0xfee;cpu.r[wr::EBX]=source;cpu.r[wr::EDI]=target;
        const auto page=(target+4095)&~4095u;
        if(reason==0){p.memory.store(frame+16,1,32);p.memory.copy_in(source,std::array<std::uint8_t,1>{0});}
        if(reason==1)p.memory.store(frame+20,64u*1024*1024+1,32);
        if(reason==2)cpu.r[wr::EBX]=target;
        if(reason==3)cpu.r[wr::EDI]=ring;
        if(reason==4)cpu.r[wr::EBX]=0xdeadbeef;
        if(reason==5 || reason==6){cpu.r[wr::EDI]=page;p.memory.protect(page,4096,reason==5?wr::Memory::Read:wr::Memory::Read|wr::Memory::Write|wr::Memory::Execute);}
        if(reason==7)p.memory.store(frame-4,1,32);
        p.memory.copy_out(target,reference_output);p.memory.copy_out(ring,reference_ring);
        std::array<std::uint8_t,64> before_frame,after_frame;p.memory.copy_out(frame-32,before_frame);
        const auto saved=cpu;
        CHECK(!wa2::special::try_lzss_prefix(cpu,p.memory));
        CHECK(cpu.r==saved.r && cpu.eip==saved.eip && cpu.flags==saved.flags && cpu.fp.bytes==saved.fp.bytes);
        p.memory.copy_out(target,actual_output);p.memory.copy_out(ring,actual_ring);p.memory.copy_out(frame-32,after_frame);
        CHECK(actual_output==reference_output && actual_ring==reference_ring && before_frame==after_frame);
        if(reason==5 || reason==6)p.memory.protect(page,4096,wr::Memory::Read|wr::Memory::Write);
        ++declines;
    }
    return cases;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);
    auto host=std::make_shared<MemoryHost>();wr::ProcessBackends backends;
    backends.gui=[host](wr::Process& p){return wa2::window::install(p,host);};
    backends.directdraw=[host](wr::Process& p){return wa2::platform::compat::install_legacy_display(p,host);};
    wr::ProcessOptions options;options.data_root=std::filesystem::current_path()/"build";
    options.image_name="WA2_Special_cn.exe";options.allow_file_write=true;
    wa2::special::Runtime runtime(options,std::move(backends),host);auto& p=runtime.process();
    const auto image=wa2::special::initial_image();
    std::string missing;
    for(const auto& item:image.imports)if(!p.resolve(item.dll,item.name))missing+=item.dll+"!"+item.name+" ";
    if(!missing.empty())throw std::runtime_error("Unbound Special imports: "+missing);
    // Load initial RAM and import thunks only; never enter the game startup.
    p.load(image);auto files=std::make_shared<MemoryFiles>();p.state().mutable_files=wa2::special::save_files(files);
    const auto kernel=[&](const char* name,std::initializer_list<U32> args){const auto target=p.resolve("kernel32.dll",name);CHECK(target);return p.callback(target,std::span(args.begin(),args.size()));};
    const auto work=p.allocate_bytes(4096);
    const auto root=p.put_wstring(u"C:\\WinRecomp");
    CHECK(kernel("SetCurrentDirectoryW",{root})==1);
    CHECK(kernel("GetCurrentDirectoryW",{0,0})==13);
    CHECK(kernel("GetCurrentDirectoryW",{128,work})==12 && p.read_wstring(work)==u"C:\\WinRecomp");
    CHECK(kernel("GetModuleFileNameW",{0,work,128})==31);
    CHECK(p.read_wstring(work)==u"C:\\WinRecomp\\WA2_Special_cn.exe");
    CHECK(kernel("GetModuleFileNameW",{0,work,4})==4 && p.last_error==122 && p.read_wstring(work)==u"C:\\");
    CHECK(kernel("LoadLibraryW",{p.put_wstring(u"kernel32.dll")})!=0);
    const auto filename=p.put_wstring(u"Sys.sav");
    const auto file=kernel("CreateFileW",{filename,0x80000000,7,0,3,0x80,0});
    CHECK(file!=0xffffffffu && files->opened=="Documents/Leaf/WHITE ALBUM2 Special Contents/Sys.sav");
    CHECK(kernel("CloseHandle",{file})==1);
    const auto invalid=p.put_wstring(u"\U0001f600.sav");
    CHECK(kernel("CreateFileW",{invalid,0x80000000,7,0,3,0x80,0})==0xffffffffu && p.last_error==1113);
    CHECK(kernel("GetDriveTypeW",{p.put_wstring(u"C:\\")})==3);
    CHECK(kernel("GetDriveTypeW",{p.put_wstring(u"Z:\\")})==1);
    CHECK(kernel("GetStartupInfoW",{work})==0 && p.memory.load(work,32)==68);
    CHECK(p.memory.load(work+56,32)==p.state().stdin_handle);
    CHECK(kernel("HeapSetInformation",{0,1,0,0})==1);
    CHECK(kernel("HeapSetInformation",{0,0,0,0})==0 && p.last_error==120);
    CHECK(kernel("IsProcessorFeaturePresent",{10})==1 && kernel("IsProcessorFeaturePresent",{40})==0);
    CHECK(kernel("IsProcessorFeaturePresent",{2})==1 && kernel("IsProcessorFeaturePresent",{3})==0);
    const auto glob=p.put_string("*.sav");
    const auto search=kernel("FindFirstFileExA",{glob,1,work,0,0,2});CHECK(search!=0xffffffffu);
    CHECK(p.read_string(work+44)=="Sys.sav");CHECK(kernel("FindClose",{search})==1);
    CHECK(kernel("FindFirstFileExA",{glob,9,work,0,0,0})==0xffffffffu && p.last_error==87);
    const auto old=files->changed;
    U32 error{};CHECK(!p.state().mutable_files->open("Documents/Leaf/WHITE ALBUM2 -closing chapter-/Sys.sav",true,true,3,error));
    CHECK(error==5 && files->changed==old);
    CHECK(p.state().mutable_files->directory("../escape")==5);
    const auto storage_probes=storage_probe_regression(p,*files);
    CHECK(p.executed_instructions==0 && p.native_function_entries==0);
    // Execute only the recovered CRT registration and its rejecting filter
    // paths, using constructed exception records. No game startup or UI.
    const auto saved_filter=p.state().unhandled_filter;
    CHECK(p.callback(4739667,{})==0 && p.state().unhandled_filter==0x485211);
    const auto pointers=work+256,record=work+512;
    p.memory.store(pointers,record,32);
    unsigned filter_cases{};
    for(const auto code:{0x80000003u,0xe06d7363u}){
        p.memory.store(record,code,32);p.memory.store(record+16,0,32);
        CHECK(p.callback(0x485211,std::array<U32,1>{pointers})==0);++filter_cases;
    }
    p.memory.store(record+16,3,32);p.memory.store(record+20,0,32);
    CHECK(p.callback(0x485211,std::array<U32,1>{pointers})==0);++filter_cases;
    p.state().unhandled_filter=saved_filter;
    CHECK(p.executed_instructions==0 && p.native_function_entries>0 && p.state().callback_depth==0);
    unsigned lzss_declines{};const auto lzss_cases=lzss_regression(p,lzss_declines);
    bool storage_counters{},cpu_counters{},completion{};
    wa2::platform::compat::emit_final_io_counters(p,"special-contents-aot",[&](std::string_view label,std::string_view value){
        if(label.ends_with(" selected_directory_io"))storage_counters=value.find("special-save-only")!=value.npos;
        if(label.ends_with(" static_cpu_work"))cpu_counters=value.find("hot_functions")!=value.npos;
        if(label.ends_with(" run_completion"))completion=value.find("\"source_returned\":false")!=value.npos;
    },false);
    CHECK(storage_counters && cpu_counters && completion);
    CHECK(host->devices==0 && host->windows==0 && host->frames==0);
    std::ofstream report(argv[1]);
    report<<"{\"schema\":\"wa2.special-runtime-check.v1\",\"bound_imports\":"<<image.imports.size()
          <<",\"wide_api_cases\":16,\"main_save_writes_rejected\":true,\"instruction_step_calls\":0,\"bounded_static_callback_entries\":"<<p.native_function_entries
          <<",\"reported_callback_address\":4739667,\"crt_initializer_registered_filter\":true,\"crt_filter_rejection_cases\":"<<filter_cases<<",\"game_startup_called\":false,"
            "\"special_final_cpu_and_storage_counters\":true,"
          <<"\"lzss_original_tail_cases\":"<<lzss_cases<<",\"lzss_output_dictionary_stack_fp_agree\":true,"
          <<"\"lzss_admission_declines_checked\":"<<lzss_declines<<','
          <<"\"unmapped_storage_probe_cases\":"<<storage_probes<<",\"unmapped_storage_provider_untouched\":true,\"canonical_save_sharing_preserved\":true,"<<
            "\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"graphics_device_opened\":false,\"complete_gameplay_verified\":false,\"passed\":true}\n";
    CHECK(bool(report));std::cout<<"PASS Special import/Unicode/storage memory checks; game entry never executed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
