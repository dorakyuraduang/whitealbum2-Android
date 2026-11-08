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
    unsigned queries{},lists{},opens{};bool fail_listing{};
    explicit Counted(std::shared_ptr<wr::MutableFiles> files):files_(std::move(files)){}
    std::optional<wr::ReadOnlyFileInfo> info(std::string_view name)override{++queries;return files_->info(name);}
    std::string identity(std::string_view name)override{return files_->identity(name);}
    std::vector<wr::ReadOnlyFileInfo> list(std::string_view name)override{++lists;if(fail_listing)throw std::runtime_error("Synthetic provider cannot list siblings");return files_->list(name);}
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
Result exercise(const std::filesystem::path& root,bool linear,bool scoped){
    const auto selected=root/(std::string(linear?"linear-":"paged-")+(scoped?"scoped":"baseline"));CHECK(std::filesystem::create_directory(selected));
    write(selected/"save_00.sav",1);write(selected/"save_09.sav",9);
    wr::ProcessOptions options;options.prefer_linear_memory=linear;options.allow_file_write=true;options.data_root=selected/"private";CHECK(std::filesystem::create_directory(options.data_root));
    wa2::aot::Runtime runtime(options);auto& p=runtime.process();auto& m=p.memory;
    auto backend=std::make_shared<Counted>(c::local_files(selected));
    p.state().mutable_files=c::save_files(backend,false,scoped?c::SaveReadScope([&]{return wa2::aot::save_read_scope(p);}):c::SaveReadScope{});
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
    if(!scoped){CHECK(result.queries==198 && result.lists==0);return result;}
    CHECK(result.queries==0 && result.lists==1);
    // Payloads are never cached, even during the same inventory update.
    write(selected/"save_00.sav",41);reader(0,true,41);++result.guards;
    write(selected/"save_01.sav",17);m.store(0xa3a130,26,32);reader(1,true,17);CHECK(backend->lists==2);++result.guards;
    std::filesystem::create_directories(selected/"Documents/Leaf/WHITE ALBUM2");write(selected/"Documents/Leaf/WHITE ALBUM2/save_05.sav",55);
    m.store(0xa3a130,27,32);reader(5,true,55);const auto at_legacy=backend->lists;reader(6,false,0);CHECK(backend->lists==at_legacy);++result.guards;
    // Own mutations invalidate before and after the operation, so a snapshot
    // rebuilt by path resolution cannot retain a removed/renamed target.
    CHECK(p.state().mutable_files->remove("save_01.sav",false)==0);reader(1,false,0);++result.guards;
    CHECK(p.state().mutable_files->move("save_00.sav","save_02.sav")==0);reader(0,false,0);reader(2,true,41);++result.guards;
    std::vector<std::uint8_t> replacement(0x10058,61);p.state().mutable_files->replace("save_02.sav",replacement);reader(2,true,61);++result.guards;
    // Ordinary story calls and worker calls use fresh per-operation metadata.
    m.store(0xa39208,0,32);write(selected/"save_03.sav",33);const auto old_queries=backend->queries;reader(3,true,33);CHECK(backend->queries>old_queries);++result.guards;
    m.store(0xa39208,7,32);p.state().current_thread_id=2;const auto worker_queries=backend->queries;reader(3,true,33);CHECK(backend->queries>worker_queries);p.state().current_thread_id=1;++result.guards;
    // An open writer disables the shared inventory until its handle closes.
    const auto name=p.put_string("save_09.sav");const auto handle=p.callback(p.resolve("kernel32.dll","CreateFileA"),std::array<U32,7>{name,0xc0000000,7,0,3,128,0});CHECK(handle!=0xffffffff);
    const auto writer_queries=backend->queries;reader(3,true,33);CHECK(backend->queries>writer_queries);
    CHECK(p.callback(p.resolve("kernel32.dll","CloseHandle"),std::array<U32,1>{handle})==1);++result.guards;
    // A stalled or reused source counter cannot keep an inventory forever.
    reader(3,true,33);const auto before_timeout=backend->lists;write(selected/"save_04.sav",44);
    std::this_thread::sleep_for(std::chrono::milliseconds(260));reader(4,true,44);CHECK(backend->lists>before_timeout);++result.guards;
    backend->fail_listing=true;m.store(0xa3a130,28,32);const auto before_decline=backend->lists;reader(2,true,61);reader(3,true,33);CHECK(backend->lists==before_decline+1);backend->fail_listing=false;++result.guards;
    CHECK(std::filesystem::create_directory(selected/"save_06.sav"));m.store(0xa3a130,29,32);reader(6,false,0);CHECK(p.last_error==5);++result.guards;
    CHECK(!p.gui() && !p.directdraw());return result;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto root=std::filesystem::path(argv[1]).parent_path()/("save-inventory-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(std::filesystem::create_directory(root));
    unsigned readers{},guards{};for(bool linear:{false,true}){const auto baseline=exercise(root,linear,false),scoped=exercise(root,linear,true);readers+=baseline.readers+scoped.readers;guards+=scoped.guards;}
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.save-inventory.v1\",\"actual_static_reader_calls\":"<<readers<<",\"guard_cases\":"<<guards
        <<",\"slots_per_fixture\":100,\"present_slots\":2,\"baseline_metadata_queries_per_fixture\":198,\"scoped_metadata_queries_per_fixture\":0,\"scoped_directory_lists_per_fixture\":1,\"descriptor_opens_per_fixture\":2"
          ",\"same_static_save_reader\":true,\"payload_reads_fresh\":true,\"write_invalidation\":true,\"source_frame_invalidation\":true,\"ordinary_and_worker_reads_fresh\":true,\"inventory_lifetime_bounded\":true,\"original_saves_opened\":false,\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"graphics_device_created\":false,\"audio_device_opened\":false,\"android_device_verified\":false,\"passed\":true}\n";
    CHECK(out.good());std::cout<<"PASS unchanged static save reader: per-slot metadata lookup replaced by one inventory for 100 own-fixture slots; guards and fresh payload reads.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
