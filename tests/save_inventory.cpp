#include "process.hpp"
#include "save_read_scope.hpp"
#include "../platform/compat/save_files.hpp"
#include <winrecomp/win32_state.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

using wr::U32;namespace c=wa2::platform::compat;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed at ")+std::to_string(__LINE__)+": "+#x);}while(false)
class Counted final:public wr::MutableFiles {
    std::shared_ptr<wr::MutableFiles> files_;
public:
    unsigned queries{},lists{},opens{},prepared_reads{};bool fail_listing{},references{};
    explicit Counted(std::shared_ptr<wr::MutableFiles> files,bool refs):files_(std::move(files)),references(refs){}
    void resolved(wr::ReadOnlyFileInfo& info,std::string name){if(references && !(info.attributes&16))info.open_read=[this,name=std::move(name)](U32& error){++prepared_reads;return open(name,true,false,3,error);};}
    std::optional<wr::ReadOnlyFileInfo> info(std::string_view name)override{++queries;auto item=files_->info(name);if(item)resolved(*item,std::string(name));return item;}
    std::string identity(std::string_view name)override{return files_->identity(name);}
    std::vector<wr::ReadOnlyFileInfo> list(std::string_view name)override{++lists;if(fail_listing)throw std::runtime_error("Synthetic provider cannot list siblings");auto items=files_->list(name);for(auto& item:items)resolved(item,name.empty()?item.name:std::string(name)+"/"+item.name);return items;}
    std::FILE* open(std::string_view name,bool read,bool write,U32 mode,U32& error)override{++opens;return files_->open(name,read,write,mode,error);}
    U32 directory(std::string_view name)override{return files_->directory(name);}
    U32 remove(std::string_view name,bool directory)override{return files_->remove(name,directory);}
    U32 move(std::string_view a,std::string_view b)override{return files_->move(a,b);}
    U32 attributes(std::string_view name,U32 flags)override{return files_->attributes(name,flags);}
    void replace(std::string_view name,std::span<const std::uint8_t> bytes)override{files_->replace(name,bytes);}
};
void write(const std::filesystem::path& path,unsigned byte){
    std::vector<std::uint8_t> data(0x10058,std::uint8_t(byte));std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(data.data()),data.size());CHECK(file.good());
}
struct Result {unsigned queries{},lists{},opens{},readers{},guards{};};
Result exercise(const std::filesystem::path& root,bool linear,bool scoped,bool references){
    const auto selected=root/(std::string(linear?"linear-":"paged-")+(scoped?"scoped":"baseline")+(references?"-references":"-paths"));CHECK(std::filesystem::create_directory(selected));const auto save_root=selected/"Documents/Leaf/WHITE ALBUM2";CHECK(std::filesystem::create_directories(save_root));
    write(save_root/"save_00.sav",1);write(save_root/"save_09.sav",9);
    wr::ProcessOptions options;options.prefer_linear_memory=linear;options.allow_file_write=true;options.data_root=selected/"private";CHECK(std::filesystem::create_directory(options.data_root));
    wa2::aot::Runtime runtime(options);auto& p=runtime.process();auto& m=p.memory;
    auto backend=std::make_shared<Counted>(c::local_files(selected),references);
    p.state().mutable_files=c::save_files(backend,false,scoped?wa2::aot::traced_save_read_scope(p):c::SaveReadScope{});
    runtime.load();CHECK(!p.gui() && !p.directdraw());
    const std::string prefix="C:\\WinRecomp";m.copy_in(0xa39d28,{reinterpret_cast<const std::uint8_t*>(prefix.c_str()),prefix.size()+1});
    m.store(0xa39208,7,32);m.store(0xa3a130,25,32);Result result;
    const auto reader=[&](unsigned slot,bool exists,unsigned byte){
        // Actual unchanged 0x4234c0 -> 0x44c4c0 -> file APIs on own headers.
        // No game startup, menu, resources, graphics or audio run.
        CHECK(p.callback(0x4234c0,std::array<U32,6>{slot,0,0,0,0,0},false)==U32(exists));
        if(exists)CHECK(m.load(0xa4e5c0+0x10057,8)==byte);++result.readers;
    };
    for(unsigned n=0;n<100;++n)reader(n,n==0 || n==9,n==0?1:9);
    result.queries=backend->queries;result.lists=backend->lists;result.opens=backend->opens;
    const auto io=p.state().reports.at("file_io")();CHECK(io.find("\"read_calls\":2,")!=std::string::npos && io.find("\"read_bytes\":131248,")!=std::string::npos);
    CHECK(result.opens==2);
    CHECK(backend->prepared_reads==(references?2u:0u));
    if(!scoped){CHECK(result.queries==100 && result.lists==0);return result;}
    CHECK(result.queries==0 && result.lists==1);
    // Payloads are never cached, even during the same inventory update.
    write(save_root/"save_00.sav",41);reader(0,true,41);++result.guards;
    write(save_root/"save_01.sav",17);m.store(0xa3a130,26,32);reader(1,true,17);CHECK(backend->lists==2);++result.guards;
    std::filesystem::create_directories(selected/"Documents/Leaf/WHITE ALBUM2");write(selected/"Documents/Leaf/WHITE ALBUM2/save_05.sav",55);
    m.store(0xa3a130,27,32);reader(5,true,55);const auto at_legacy=backend->lists;reader(6,false,0);CHECK(backend->lists==at_legacy);++result.guards;
    // Own mutations invalidate before and after the operation, so a snapshot
    // rebuilt by path resolution cannot retain a removed/renamed target.
    CHECK(p.state().mutable_files->remove("save_01.sav",false)==0);reader(1,false,0);++result.guards;
    CHECK(p.state().mutable_files->move("save_00.sav","save_02.sav")==0);reader(0,false,0);reader(2,true,41);++result.guards;
    std::vector<std::uint8_t> replacement(0x10058,61);p.state().mutable_files->replace("save_02.sav",replacement);reader(2,true,61);++result.guards;
    // Title-menu readers do not enter story substates 6/7. They still share
    // a source-update inventory, and a state change creates a new snapshot.
    m.store(0xa39208,0,32);write(save_root/"save_03.sav",33);const auto old_lists=backend->lists;reader(3,true,33);CHECK(backend->lists>old_lists);++result.guards;
    const auto title_lists=backend->lists;reader(3,true,33);CHECK(backend->lists==title_lists);++result.guards;
    // Ordinary API calls remain fresh even while a source inventory exists.
    write(save_root/"save_07.sav",77);const auto old_queries=backend->queries;
    const auto fresh=p.callback(p.resolve("kernel32.dll","CreateFileA"),std::array<U32,7>{p.put_string("save_07.sav"),0x80000000,7,0,3,128,0});CHECK(fresh!=0xffffffff && backend->queries>old_queries);
    CHECK(p.callback(p.resolve("kernel32.dll","CloseHandle"),std::array<U32,1>{fresh})==1);++result.guards;
    m.store(0xa39208,7,32);p.state().current_thread_id=2;const auto worker_queries=backend->queries;reader(3,true,33);CHECK(backend->queries>worker_queries);p.state().current_thread_id=1;++result.guards;
    // An open writer disables the shared inventory until its handle closes.
    const auto name=p.put_string("save_09.sav"),before_writer=backend->prepared_reads;const auto handle=p.callback(p.resolve("kernel32.dll","CreateFileA"),std::array<U32,7>{name,0xc0000000,7,0,3,128,0});CHECK(handle!=0xffffffff && backend->prepared_reads==before_writer);
    const auto writer_queries=backend->queries;reader(3,true,33);CHECK(backend->queries>writer_queries);
    CHECK(p.callback(p.resolve("kernel32.dll","CloseHandle"),std::array<U32,1>{handle})==1);++result.guards;
    // A stalled or reused source counter cannot keep an inventory forever.
    reader(3,true,33);const auto before_timeout=backend->lists;write(save_root/"save_04.sav",44);
    std::this_thread::sleep_for(std::chrono::milliseconds(260));reader(4,true,44);CHECK(backend->lists>before_timeout);++result.guards;
    backend->fail_listing=true;m.store(0xa3a130,28,32);const auto before_decline=backend->lists;reader(2,true,61);reader(3,true,33);CHECK(backend->lists==before_decline+1);backend->fail_listing=false;++result.guards;
    CHECK(std::filesystem::create_directory(save_root/"save_06.sav"));m.store(0xa3a130,29,32);reader(6,false,0);CHECK(p.last_error==5);++result.guards;
    CHECK(p.state().reports.at("save_read_scope")().find("\"contexts\":[{")!=std::string::npos);
    CHECK(!p.gui() && !p.directdraw());return result;
}
unsigned chapter_index(const std::filesystem::path& root,bool linear,bool scoped,bool missing){
    const auto selected=root/(std::string("index-")+(linear?"linear-":"paged-")+(scoped?"batch-":"baseline-")+(missing?"absent":"present"));CHECK(std::filesystem::create_directory(selected));const auto save_root=selected/"Documents/Leaf/WHITE ALBUM2";CHECK(std::filesystem::create_directories(save_root));
    write(save_root/"save_00.sav",0x11);write(save_root/"save_09.sav",0x19);
    const std::string ic="Documents/Leaf/WHITE ALBUM2 -introductory chapter-",cc="Documents/Leaf/WHITE ALBUM2 -closing chapter-";
    if(!missing){CHECK(std::filesystem::create_directories(selected/ic));write(selected/ic/"save_00.sav",0xa1);write(selected/ic/"save_09.sav",0xa9);}
    wr::ProcessOptions options;options.data_root=selected/"private";options.prefer_linear_memory=linear;options.allow_file_write=true;CHECK(std::filesystem::create_directory(options.data_root));
    wa2::aot::Runtime runtime(options);auto& p=runtime.process();auto& m=p.memory;
    auto backend=std::make_shared<Counted>(c::local_files(selected),true);
    p.state().mutable_files=c::save_files(backend,false,scoped?wa2::aot::traced_save_read_scope(p):c::SaveReadScope{});runtime.load();
    const auto set_path=[&](U32 address,std::string text){m.copy_in(address,{reinterpret_cast<const std::uint8_t*>(text.c_str()),text.size()+1});};
    set_path(0xa39d28,"C:\\WinRecomp");set_path(0xa39270,"C:\\WinRecomp\\"+ic);m.store(0xa3a130,37,32);m.store(0xa39208,7,32);
    const auto output=p.allocate_bytes(16);unsigned calls{};
    const auto read=[&](U32 slot,U32 chapter,bool exists,U32 byte){
        // The actual first-list timestamp helper reads 24 bytes, unlike the
        // 65624-byte preview helper used by the earlier fixture.
        CHECK(p.callback(0x4233c0,std::array<U32,3>{slot,output,chapter},false)==U32(exists));++calls;
        if(exists)for(unsigned n=0;n<16;++n)CHECK(m.load(output+n,8)==byte);
    };
    for(U32 n=0;n<100;++n)read(n,0,n==0 || n==9,n==0?0x11:0x19);
    for(U32 n=0;n<100;++n)read(n,1,!missing && (n==0 || n==9),n==0?0xa1:0xa9);
    CHECK(backend->opens==(missing?2u:4u));
    if(scoped)CHECK(backend->queries==0 && backend->lists==2);
    else CHECK(backend->queries==200 && backend->lists==0);
    if(scoped && !missing){
        write(selected/ic/"save_00.sav",0xb1);read(0,1,true,0xb1);read(0,0,true,0x11);
        CHECK(std::filesystem::create_directories(selected/cc));write(selected/cc/"save_00.sav",0xc1);set_path(0xa39270,"C:\\WinRecomp\\"+cc);read(0,1,true,0xc1);
        set_path(0xa39270,"C:\\WinRecomp\\"+ic);read(0,1,true,0xb1);
        std::vector<std::uint8_t> next(0x10058,0xb2);p.state().mutable_files->replace(ic+"/save_01.sav",next);read(1,1,true,0xb2);
        write(selected/ic/"save_02.sav",0xb3);m.store(0xa3a130,38,32);read(2,1,true,0xb3);
        backend->fail_listing=true;m.store(0xa3a130,39,32);read(0,1,true,0xb1);backend->fail_listing=false;
    }
    CHECK(!p.gui() && !p.directdraw());return calls;
}
unsigned full_bank(const std::filesystem::path& root){
    const auto selected=root/"all-100-slots",bank=selected/"Documents/Leaf/WHITE ALBUM2";
    CHECK(std::filesystem::create_directories(bank));
    for(unsigned slot=0;slot<100;++slot){
        const auto name="save_"+std::string(1,char('0'+slot/10))+char('0'+slot%10)+".sav";
        std::vector<std::uint8_t> bytes(0x10058,std::uint8_t(slot));std::fill_n(bytes.begin(),88,0);bytes[0]=2;
        const auto date=[&](unsigned at,unsigned value){bytes[at]=std::uint8_t(value);bytes[at+1]=std::uint8_t(value>>8);};
        date(8,2026);date(10,11);date(12,3);date(14,11);
        const auto label="Own slot "+std::to_string(slot);std::copy(label.begin(),label.end(),bytes.begin()+24);
        std::ofstream output(bank/name,std::ios::binary);output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());CHECK(output.good());
        write(selected/name,0xee);
    }
    wr::ProcessOptions options;options.data_root=selected/"private";options.prefer_linear_memory=true;CHECK(std::filesystem::create_directory(options.data_root));
    wa2::aot::Runtime runtime(options);auto& p=runtime.process();auto& m=p.memory;
    auto backend=std::make_shared<Counted>(c::local_files(selected),true);
    p.state().mutable_files=c::save_files(backend,false,wa2::aot::traced_save_read_scope(p));runtime.load();
    const std::string path="C:\\WinRecomp";m.copy_in(0xa39d28,{reinterpret_cast<const std::uint8_t*>(path.c_str()),path.size()+1});
    m.store(0xa39208,7,32);m.store(0xa3a130,71,32);
    for(unsigned slot=0;slot<100;++slot){
        CHECK(p.callback(0x4234c0,std::array<U32,6>{slot,0,0,0,0,0},false)==1);
        CHECK(m.load(0xa4e5c0,32)==2 && m.load(0xa4e5c0+0x10057,8)==slot);
    }
    CHECK(backend->queries==0 && backend->lists==1 && backend->opens==100 && backend->prepared_reads==100);
    CHECK(!p.gui() && !p.directdraw());return 100;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto root=std::filesystem::path(argv[1]).parent_path()/("save-inventory-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(std::filesystem::create_directory(root));
    unsigned readers{},guards{};for(bool linear:{false,true})for(bool references:{false,true}){const auto baseline=exercise(root,linear,false,references),scoped=exercise(root,linear,true,references);readers+=baseline.readers+scoped.readers;guards+=scoped.guards;}
    unsigned index_calls{};for(bool linear:{false,true})for(bool scoped:{false,true})for(bool missing:{false,true})index_calls+=chapter_index(root,linear,scoped,missing);
    readers+=full_bank(root);
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.save-inventory.v1\",\"actual_static_reader_calls\":"<<readers<<",\"guard_cases\":"<<guards
        <<",\"actual_static_index_calls\":"<<index_calls<<",\"chapter_index_batch\":true,\"chapter_namespace_separate\":true,\"missing_chapter_queries_batched\":true,\"baseline_two_namespace_metadata_queries\":200,\"batched_two_namespace_directory_queries\":2,\"batched_absent_chapter_directory_queries\":2,\"full_bank_slots\":100,\"full_bank_single_inventory\":true,\"root_previews_ignored\":true"
          ",\"slots_per_fixture\":100,\"present_slots\":2,\"baseline_metadata_queries_per_fixture\":100,\"scoped_metadata_queries_per_fixture\":0,\"scoped_directory_lists_per_fixture\":1,\"descriptor_opens_per_fixture\":2"
          ",\"same_static_save_reader\":true,\"prepared_reads_checked\":true,\"writers_bypass_read_reference\":true,\"title_reader_batch\":true,\"scope_audit_checked\":true,\"payload_reads_fresh\":true,\"write_invalidation\":true,\"source_frame_invalidation\":true,\"ordinary_and_worker_reads_fresh\":true,\"inventory_lifetime_bounded\":true,\"original_saves_opened\":false,\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"graphics_device_created\":false,\"audio_device_opened\":false,\"android_device_verified\":false,\"passed\":true}\n";
    CHECK(out.good());std::cout<<"PASS unchanged static save reader: per-slot metadata lookup replaced by one inventory for 100 own-fixture slots; guards and fresh payload reads.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
