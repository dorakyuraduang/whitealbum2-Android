#include "process.hpp"
#include "../platform/compat/system.hpp"
#include "../platform/compat/profile.hpp"
#include "../platform/compat/save_files.hpp"
#include <winrecomp/win32_state.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using wr::U32;namespace c=wa2::platform::compat;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed at ")+std::to_string(__LINE__)+": "+#x);}while(false)
class Counted final:public wr::MutableFiles {
    std::shared_ptr<wr::MutableFiles> files_;
public:
    unsigned queries{},opens{},prepared_reads{};bool fail_open{};
    explicit Counted(std::shared_ptr<wr::MutableFiles> files):files_(std::move(files)){}
    std::optional<wr::ReadOnlyFileInfo> info(std::string_view name)override{++queries;auto item=files_->info(name);if(item && !(item->attributes&16))item->open_read=[this,path=std::string(name)](U32& error){++prepared_reads;return open(path,true,false,3,error);};return item;}
    std::vector<wr::ReadOnlyFileInfo> list(std::string_view name)override{return files_->list(name);}
    std::FILE* open(std::string_view name,bool r,bool w,U32 mode,U32& error)override{++opens;if(fail_open){error=5;return nullptr;}return files_->open(name,r,w,mode,error);}
    U32 directory(std::string_view name)override{return files_->directory(name);}
    U32 remove(std::string_view name,bool directory)override{return files_->remove(name,directory);}
    U32 move(std::string_view from,std::string_view to)override{return files_->move(from,to);}
    U32 attributes(std::string_view name,U32 flags)override{return files_->attributes(name,flags);}
    void replace(std::string_view name,std::span<const std::uint8_t> bytes)override{files_->replace(name,bytes);}
};
void put(const std::filesystem::path& path,unsigned value){std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<"[DEFAULT]\r\nvalue="<<value<<"\r\n";CHECK(file.good());}
unsigned exercise(const std::filesystem::path& parent,bool linear,bool cached,unsigned& calls){
    const auto root=parent/(std::string(linear?"linear-":"paged-")+(cached?"batch":"baseline"));CHECK(std::filesystem::create_directory(root));
    {std::ofstream out(root/"SYSTEM.ini");out<<"[DEFAULT]\n";for(unsigned n=0;n<100;++n)out<<'k'<<n<<'='<<n*3<<'\n';}
    wr::ProcessOptions options;options.data_root=root/"private";options.allow_file_write=true;options.prefer_linear_memory=linear;CHECK(std::filesystem::create_directory(options.data_root));
    wa2::aot::Runtime runtime(options);auto& p=runtime.process();auto& m=p.memory;
    if(!cached)c::install_system(p); // Same source wrapper, previous unbatched API.
    auto backend=std::make_shared<Counted>(c::local_files(root));p.state().mutable_files=c::save_files(backend,false);
    runtime.load();CHECK(!p.gui() && !p.directdraw());m.store(0xa3a130,11,32);
    const auto section=p.put_string("DEFAULT"),file=p.put_string("SYSTEM.ini"),value=p.put_string("value");
    const auto getter=[&](U32 key,U32 fallback,U32 name){++calls;return p.callback(0x44d4e0,std::array<U32,4>{key,fallback,section,name},false);};
    for(unsigned n=0;n<100;++n)CHECK(getter(p.put_string("k"+std::to_string(n)),999,file)==n*3);
    CHECK(backend->opens==(cached?1u:100u) && backend->queries==(cached?1u:100u));
    CHECK(backend->prepared_reads==backend->opens);
    if(!cached)return 0;
    unsigned guards{};
    const auto missing=p.put_string("default.ini");backend->opens=0;
    for(unsigned n=0;n<100;++n)CHECK(getter(value,n,missing)==n);
    CHECK(backend->opens==1);++guards;
    put(root/"SYSTEM.ini",42);m.store(0xa3a130,12,32);CHECK(getter(value,0,file)==42);++guards;
    const auto write=p.resolve("kernel32.dll","WritePrivateProfileStringA");
    CHECK(p.callback(write,std::array<U32,4>{section,value,p.put_string("61"),file})==1);
    CHECK(c::Profile::read(root/"SYSTEM.ini").get("DEFAULT","value")=="61" && getter(value,0,file)==61);++guards;
    // Low-level native configuration writes invalidate the same snapshot too.
    auto changed=c::read_profile(p,wr::guest_file_path(p,"SYSTEM.ini"));changed.set("DEFAULT","value","73");c::save_profile(p,changed,wr::guest_file_path(p,"SYSTEM.ini"));CHECK(getter(value,0,file)==73);++guards;
    put(root/"SYSTEM.ini",81);CHECK(p.callback(write,std::array<U32,4>{0,0,0,file})==1 && getter(value,0,file)==81);++guards;
    // Ordinary API callers and logical workers retain fresh reads.
    const auto direct=p.resolve("kernel32.dll","GetPrivateProfileIntA");put(root/"SYSTEM.ini",91);
    CHECK(p.callback(direct,std::array<U32,4>{section,value,0,file})==91);++guards;
    put(root/"SYSTEM.ini",93);p.state().current_thread_id=2;CHECK(getter(value,0,file)==93);p.state().current_thread_id=1;++guards;
    CHECK(getter(value,0,file)==93);put(root/"SYSTEM.ini",97);std::this_thread::sleep_for(std::chrono::milliseconds(260));CHECK(getter(value,0,file)==97);++guards;
    // An open source writer disables caching, and its write is immediately
    // visible. Re-enabling after close cannot revive an older revision.
    const auto handle=p.callback(p.resolve("kernel32.dll","CreateFileA"),std::array<U32,7>{file,0xc0000000,7,0,3,128,0});CHECK(handle!=0xffffffff);
    put(root/"SYSTEM.ini",101);CHECK(getter(value,0,file)==101);CHECK(p.callback(p.resolve("kernel32.dll","CloseHandle"),std::array<U32,1>{handle})==1);CHECK(getter(value,0,file)==101);++guards;
    backend->fail_open=true;m.store(0xa3a130,13,32);bool failed=false;const auto saved=p.cpu;
    try{getter(value,0,file);}catch(const std::runtime_error&){failed=true;p.cpu=saved;}CHECK(failed);backend->fail_open=false;CHECK(getter(value,0,file)==101);++guards;
    // Replacing the mounted namespace cannot reuse another directory's INI.
    const auto other=root/"Other";CHECK(std::filesystem::create_directory(other));put(other/"SYSTEM.ini",131);
    p.state().mutable_files=c::save_files(c::local_files(other),false);CHECK(getter(value,0,file)==131);++guards;
    const auto report=p.state().reports.at("profile_io")();CHECK(report.find("\"writes_deferred\":false")!=std::string::npos);
    CHECK(!p.gui() && !p.directdraw());return guards;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto root=std::filesystem::path(argv[1]).parent_path()/("profile-batch-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(std::filesystem::create_directory(root));
    unsigned calls{},guards{};for(bool linear:{false,true}){exercise(root,linear,false,calls);guards+=exercise(root,linear,true,calls);}
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.profile-batch.v1\",\"static_profile_getter_calls\":"<<calls<<",\"guard_cases\":"<<guards
        <<",\"keys_per_batch\":100,\"baseline_descriptor_opens\":100,\"batched_descriptor_opens\":1,\"missing_profile_opens_per_100_defaults\":1,\"same_static_getter\":true,\"writes_immediate\":true,\"flush_and_revision_invalidation\":true,\"frame_and_age_bounded\":true,\"ordinary_and_worker_reads_fresh\":true,\"namespace_isolation\":true,\"failed_read_retry\":true,\"game_launched\":false,\"screen_access\":false,\"source_executable_opened\":false,\"original_files_opened\":false,\"android_device_verified\":false,\"passed\":true}\n";
    CHECK(out.good());std::cout<<"PASS unchanged static profile getter: 100 key reads use one external profile open; write, frame, namespace and retry guards.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
