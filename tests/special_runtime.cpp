#include "runtime.hpp"
#include "../port/guest_api.hpp"
#include "../platform/window/gui.hpp"
#include "../platform/compat/legacy_display.hpp"
#include <winrecomp/win32_state.hpp>
#include <fstream>
#include <iostream>
#include <set>

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
    std::set<std::string> directories{""};
    std::optional<wr::ReadOnlyFileInfo> info(std::string_view name)override{
        if(directories.contains(std::string(name)))return wr::ReadOnlyFileInfo{std::string(name),0,0,16};
        if(name.ends_with("Sys.sav") || name.ends_with("sys.sav"))return wr::ReadOnlyFileInfo{"Sys.sav",4,0,128};
        return {};
    }
    std::vector<wr::ReadOnlyFileInfo> list(std::string_view name)override{listed=name;return {{"Sys.sav",4,0,128},{"save_00.sav",4,0,128},{"bad.pak",4,0,128}};}
    std::FILE* open(std::string_view name,bool,bool write,U32,U32& error)override{
        opened=name;if(write)changed=name;error=0;auto* file=std::tmpfile();if(file){std::fwrite("TEST",1,4,file);std::rewind(file);}else error=5;return file;
    }
    U32 directory(std::string_view name)override{return directories.emplace(name).second?0:183;}
    U32 remove(std::string_view name,bool)override{changed=name;return 0;}
    U32 move(std::string_view,std::string_view name)override{changed=name;return 0;}
    U32 attributes(std::string_view name,U32)override{changed=name;return 0;}
    void replace(std::string_view name,std::span<const std::uint8_t>)override{changed=name;}
};
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
    CHECK(host->devices==0 && host->windows==0 && host->frames==0);
    std::ofstream report(argv[1]);
    report<<"{\"schema\":\"wa2.special-runtime-check.v1\",\"bound_imports\":"<<image.imports.size()
          <<",\"wide_api_cases\":16,\"main_save_writes_rejected\":true,\"instruction_step_calls\":0,\"bounded_static_callback_entries\":"<<p.native_function_entries
          <<",\"reported_callback_address\":4739667,\"crt_initializer_registered_filter\":true,\"crt_filter_rejection_cases\":"<<filter_cases<<",\"game_startup_called\":false,"
            "\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"graphics_device_opened\":false,\"complete_gameplay_verified\":false,\"passed\":true}\n";
    CHECK(bool(report));std::cout<<"PASS Special import/Unicode/storage memory checks; game entry never executed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
