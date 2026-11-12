#include "process.hpp"
#include "../recompiled/save_read_scope.hpp"
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
    unsigned queries{},opens{},prepared_reads{},replaces{};bool fail_open{},fail_replace{};
    explicit Counted(std::shared_ptr<wr::MutableFiles> files):files_(std::move(files)){}
    std::optional<wr::ReadOnlyFileInfo> info(std::string_view name)override{++queries;auto item=files_->info(name);if(item && !(item->attributes&16))item->open_read=[this,path=std::string(name)](U32& error){++prepared_reads;return open(path,true,false,3,error);};return item;}
    std::vector<wr::ReadOnlyFileInfo> list(std::string_view name)override{return files_->list(name);}
    std::FILE* open(std::string_view name,bool r,bool w,U32 mode,U32& error)override{++opens;if(fail_open){error=5;return nullptr;}return files_->open(name,r,w,mode,error);}
    U32 directory(std::string_view name)override{return files_->directory(name);}
    U32 remove(std::string_view name,bool directory)override{return files_->remove(name,directory);}
    U32 move(std::string_view from,std::string_view to)override{return files_->move(from,to);}
    U32 attributes(std::string_view name,U32 flags)override{return files_->attributes(name,flags);}
    void replace(std::string_view name,std::span<const std::uint8_t> bytes)override{++replaces;if(fail_replace)throw std::runtime_error("Injected profile publication failure");files_->replace(name,bytes);}
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
struct WriteChecks {unsigned calls{},stacks{},guards{};};
WriteChecks exercise_writes(const std::filesystem::path& parent,bool linear,bool cached){
    const auto selected=parent/(std::string(linear?"write-linear-":"write-paged-")+(cached?"batch":"baseline"));CHECK(std::filesystem::create_directory(selected));
    {std::ofstream file(selected/"SYSTEM.ini");file<<"[DEFAULT]\n";for(unsigned n=0;n<53;++n)file<<'k'<<n<<'='<<n<<'\n';}
    wr::ProcessOptions options;options.data_root=selected/"private";options.allow_file_write=true;options.prefer_linear_memory=linear;CHECK(std::filesystem::create_directory(options.data_root));
    wa2::aot::Runtime runtime(options);auto& p=runtime.process();auto& m=p.memory;CHECK(!p.gui() && !p.directdraw());
    auto backend=std::make_shared<Counted>(c::local_files(selected));p.state().mutable_files=c::save_files(backend,false);
    WriteChecks result;U32 origin=0x4338d9,frame=100;
    // Execute only the unchanged integer-setting wrapper, on own files. At
    // admission substitute its outer return identity and immediately restore
    // the callback sentinel. Source main/exit/UI code is never executed.
    c::ProfileWriteScope scope=[&](wr::Process&)->std::optional<std::uint64_t>{
        const auto sp=p.cpu.r[wr::ESP];if(m.load(sp,32)!=0x44d4ba)return wa2::aot::profile_write_scope(p);
        const auto outer=m.load(sp+52,32);CHECK(outer==0xffffffd0);++result.stacks;
        m.store(sp+52,origin,32);const auto admitted=wa2::aot::profile_write_scope(p);m.store(sp+52,outer,32);return admitted;
    };
    c::install_system(p,{},wa2::aot::profile_read_scope,cached?scope:c::ProfileWriteScope{});
    runtime.load();m.store(0xa3a130,frame,32);
    const auto section=p.put_string("DEFAULT"),file=p.put_string("SYSTEM.ini");std::array<U32,53> keys;for(unsigned n=0;n<53;++n)keys[n]=p.put_string("k"+std::to_string(n));
    const auto setter=[&](unsigned n,U32 value){++result.calls;return p.callback(0x44d460,std::array<U32,4>{keys[n],value,section,file},false);};
    for(unsigned n=0;n<53;++n)CHECK(setter(n,n)==0);
    CHECK(backend->opens==(cached?1u:53u) && backend->replaces==(cached?0u:53u));
    if(!cached)return result;
    // One changed value commits before return, and the 52 unchanged values
    // reuse that successful publication without any additional file writes.
    m.store(0xa3a130,++frame,32);auto opens=backend->opens,writes=backend->replaces;
    CHECK(setter(0,1000)==0 && c::Profile::read(selected/"SYSTEM.ini").get("DEFAULT","k0")=="1000");
    for(unsigned n=1;n<53;++n)CHECK(setter(n,n)==0);
    CHECK(backend->opens==opens+1 && backend->replaces==writes+1);++result.guards;
    // Every changed value remains immediately visible, never staged until
    // another call or process teardown. Include the last certified caller.
    m.store(0xa3a130,++frame,32);writes=backend->replaces;
    for(unsigned n=0;n<53;++n){if(n==52)origin=0x433dbc;CHECK(setter(n,n+200)==0);CHECK(c::Profile::read(selected/"SYSTEM.ini").get("DEFAULT","k"+std::to_string(n))==std::to_string(n+200));}
    CHECK(backend->replaces==writes+53);origin=0x4338d9;++result.guards;
    const auto direct=p.resolve("kernel32.dll","WritePrivateProfileStringA");
    writes=backend->replaces;CHECK(p.callback(direct,std::array<U32,4>{section,keys[0],p.put_string("200"),file})==1 && backend->replaces==writes+1);++result.guards;
    for(auto caller:{0x4338d8u,0x44d510u,0xffffffd0u}){origin=caller;opens=backend->opens;writes=backend->replaces;CHECK(setter(0,200)==0 && backend->opens==opens+1 && backend->replaces==writes+1);++result.guards;}origin=0x4338d9;
    p.state().current_thread_id=2;writes=backend->replaces;CHECK(setter(0,200)==0 && backend->replaces==writes+1);p.state().current_thread_id=1;++result.guards;
    const auto hold=p.put_string("D3D.ini");const auto handle=p.callback(p.resolve("kernel32.dll","CreateFileA"),std::array<U32,7>{hold,0xc0000000,7,0,2,128,0});CHECK(handle!=0xffffffff);writes=backend->replaces;CHECK(setter(0,200)==0 && backend->replaces==writes+1);CHECK(p.callback(p.resolve("kernel32.dll","CloseHandle"),std::array<U32,1>{handle})==1);++result.guards;
    // Explicit flush, source-frame changes and non-sliding age all refresh.
    CHECK(p.callback(direct,std::array<U32,4>{0,0,0,file})==1);opens=backend->opens;CHECK(setter(0,200)==0 && backend->opens==opens+1);++result.guards;
    auto edited=c::Profile::read(selected/"SYSTEM.ini");edited.set("DEFAULT","k0","999");edited.save(selected/"SYSTEM.ini");m.store(0xa3a130,++frame,32);CHECK(setter(0,200)==0 && c::Profile::read(selected/"SYSTEM.ini").get("DEFAULT","k0")=="200");++result.guards;
    CHECK(p.callback(direct,std::array<U32,4>{0,0,0,file})==1);opens=backend->opens;CHECK(setter(0,200)==0);std::this_thread::sleep_for(std::chrono::milliseconds(140));CHECK(setter(0,200)==0);std::this_thread::sleep_for(std::chrono::milliseconds(140));CHECK(setter(0,200)==0 && backend->opens>=opens+2);++result.guards;
    edited.set("DEFAULT","k0","800");p.state().mutable_files->replace("SYSTEM.ini",edited.bytes());CHECK(setter(0,200)==0 && c::Profile::read(selected/"SYSTEM.ini").get("DEFAULT","k0")=="200");++result.guards;
    // Neither a failed read nor a failed immediate commit seeds a snapshot.
    m.store(0xa3a130,++frame,32);backend->fail_open=true;auto saved=p.cpu;bool failed=false;try{setter(0,300);}catch(const std::runtime_error&){failed=true;p.cpu=saved;}CHECK(failed);backend->fail_open=false;CHECK(setter(0,300)==0);++result.guards;
    backend->fail_replace=true;saved=p.cpu;failed=false;try{setter(0,400);}catch(const std::runtime_error&){failed=true;p.cpu=saved;}CHECK(failed && c::Profile::read(selected/"SYSTEM.ini").get("DEFAULT","k0")=="300");backend->fail_replace=false;opens=backend->opens;CHECK(setter(0,400)==0 && backend->opens==opens+1 && c::Profile::read(selected/"SYSTEM.ini").get("DEFAULT","k0")=="400");++result.guards;
    p.options.allow_file_write=false;writes=backend->replaces;CHECK(setter(0,500)==1 && backend->replaces==writes);p.options.allow_file_write=true;++result.guards;
    const auto other=selected/"Other";CHECK(std::filesystem::create_directory(other));{std::ofstream out(other/"SYSTEM.ini");out<<"[DEFAULT]\nk0=999\n";}
    auto second=std::make_shared<Counted>(c::local_files(other));p.state().mutable_files=c::save_files(second,false);CHECK(setter(0,400)==0 && second->opens==1 && second->replaces==1 && c::Profile::read(other/"SYSTEM.ini").get("DEFAULT","k0")=="400");++result.guards;
    const auto sp=p.cpu.r[wr::ESP];p.cpu.r[wr::ESP]=0xfffffffe;CHECK(!wa2::aot::profile_write_scope(p));p.cpu.r[wr::ESP]=sp;++result.guards;
    const auto report=p.state().reports.at("profile_write_io")();CHECK(report.find("\"writes_deferred\":false")!=std::string::npos);CHECK(!p.gui() && !p.directdraw());return result;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto root=std::filesystem::path(argv[1]).parent_path()/("profile-batch-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(std::filesystem::create_directory(root));
    unsigned calls{},guards{};for(bool linear:{false,true}){exercise(root,linear,false,calls);guards+=exercise(root,linear,true,calls);}
    WriteChecks writes;for(bool linear:{false,true})for(bool cached:{false,true}){const auto result=exercise_writes(root,linear,cached);writes.calls+=result.calls;writes.stacks+=result.stacks;writes.guards+=result.guards;}
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.profile-batch.v1\",\"static_profile_getter_calls\":"<<calls<<",\"guard_cases\":"<<guards
        <<",\"static_profile_setter_calls\":"<<writes.calls<<",\"setter_real_stack_checks\":"<<writes.stacks<<",\"setter_guard_cases\":"<<writes.guards<<",\"unchanged_exit_keys\":53,\"baseline_exit_profile_loads\":53,\"optimized_exit_profile_loads\":1,\"baseline_unchanged_exit_commits\":53,\"optimized_unchanged_exit_commits\":0,\"changed_writes_still_immediate\":true,\"ordinary_profile_writes_unchanged\":true,\"write_scope_failure_age_and_namespace_guards\":true,\"source_exit_and_ui_not_executed\":true"
        <<",\"keys_per_batch\":100,\"baseline_descriptor_opens\":100,\"batched_descriptor_opens\":1,\"missing_profile_opens_per_100_defaults\":1,\"same_static_getter\":true,\"writes_immediate\":true,\"flush_and_revision_invalidation\":true,\"frame_and_age_bounded\":true,\"ordinary_and_worker_reads_fresh\":true,\"namespace_isolation\":true,\"failed_read_retry\":true,\"game_launched\":false,\"screen_access\":false,\"source_executable_opened\":false,\"original_files_opened\":false,\"android_device_verified\":false,\"passed\":true}\n";
    CHECK(out.good());std::cout<<"PASS unchanged static profile getter: 100 key reads use one external profile open; write, frame, namespace and retry guards.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
