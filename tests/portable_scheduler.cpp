#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include "../platform/compat/scheduler.hpp"
#include <fstream>
#include <iostream>
#include <thread>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
using namespace wr;
constexpr U32 Start=0x401000,Acquired=0x401010,Awake=0x401020;
wr::Image image(){
    wr::Image value;value.base=0x400000;value.size=0x10000;value.headers_size=4096;value.entry=Start;value.bytes.resize(8192,0x90);
    value.sections={{".text",4096,4096,4096,4096,0x60000020}};return value;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);Process* owner{};U32 wait_api{},sleep_api{};
    // An explicit synthetic compiled procedure tests scheduling and ABI. It
    // has three fixed native labels and no instruction decoding or game code.
    Process process(FunctionExecutor{[&](Cpu& cpu,Memory& memory){
        if(cpu.eip==Start){
            const auto data=memory.load(cpu.r[ESP]+4,32);push(cpu,memory,cpu.r[EBX]);cpu.r[EBX]=data;
            owner->state().tls[77]=owner->state().current_thread_id;
            if(const auto mutex=memory.load(data+8,32)){
                push(cpu,memory,0xffffffff);push(cpu,memory,mutex);push(cpu,memory,Acquired);cpu.eip=wait_api;
            }else cpu.eip=Acquired;
            return true;
        }
        if(cpu.eip==Acquired){
            CHECK(owner->state().tls[77]==owner->state().current_thread_id);
            const auto data=cpu.r[EBX];memory.store(data,memory.load(data,32)+1,32);memory.store(data+12,owner->state().current_thread_id,32);
            push(cpu,memory,0);push(cpu,memory,Awake);cpu.eip=sleep_api;return true;
        }
        if(cpu.eip==Awake){
            cpu.r[EAX]=memory.load(cpu.r[EBX],32);cpu.r[EBX]=pop(cpu,memory);cpu.eip=pop(cpu,memory);cpu.r[ESP]+=4;return true;
        }
        return false;
    }});
    owner=&process;wa2::platform::compat::install_scheduler(process);process.load(image());
    wait_api=process.resolve("kernel32.dll","WaitForSingleObject");sleep_api=process.resolve("kernel32.dll","Sleep");
    const auto api=[&](const char* name,std::initializer_list<U32> args){const auto target=process.resolve("kernel32.dll",name);CHECK(target);return process.callback(target,{args.begin(),args.size()});};
    const auto poll=[&]{for(auto& callback:process.state().poll)callback();};
    process.state().tls[77]=0x12345678;
    const auto scratch=process.allocate_bytes(4096);const auto main=process.cpu;
    const auto mutex=api("CreateMutexA",{0,1,0});CHECK(mutex);process.memory.store(scratch+8,mutex,32);
    const auto thread=api("CreateThread",{0,0,Start,scratch,0,0});CHECK(thread);
    poll();CHECK(process.cpu.r==main.r && process.cpu.eip==main.eip);
    CHECK(process.memory.load(scratch,32)==0 && process.state().tls[77]==0x12345678);
    CHECK(api("WaitForSingleObject",{thread,0})==258);
    CHECK(api("ReleaseMutex",{mutex})==1);poll();poll();
    CHECK(api("WaitForSingleObject",{thread,0})==0);
    CHECK(api("GetExitCodeThread",{thread,scratch+24})==1 && process.memory.load(scratch+24,32)==1);
    CHECK(process.memory.load(scratch+12,32)==2 && process.state().tls[77]==0x12345678);
    // A mutex held by an exiting logical thread becomes abandoned. Its next
    // acquisition reports that state instead of hanging or silently owning it.
    CHECK(api("WaitForSingleObject",{mutex,0})==0x80);CHECK(api("ReleaseMutex",{mutex})==1);
    CHECK(api("CloseHandle",{thread})==1);CHECK(api("CloseHandle",{mutex})==1);poll();
    const auto event=api("CreateEventA",{0,0,0,0});CHECK(event);
    auto signal=process.state().events.at(event)->signal;
    {std::jthread producer([signal]{signal();});CHECK(api("WaitForSingleObject",{event,2000})==0);}
    CHECK(api("WaitForSingleObject",{event,0})==258);CHECK(api("CloseHandle",{event})==1);
    // Closed thread objects must be reclaimed even after the last worker
    // exits. More than the live-thread cap can be created sequentially.
    process.memory.store(scratch+8,0,32);
    for(unsigned i=0;i<128;++i){
        const auto handle=api("CreateThread",{0,0,Start,scratch,4,0});CHECK(handle);
        CHECK(api("ResumeThread",{handle})==1);poll();poll();
        CHECK(api("WaitForSingleObject",{handle,0})==0);CHECK(api("CloseHandle",{handle})==1);poll();
    }
    CHECK(process.memory.load(scratch,32)==129 && process.state().tls[77]==0x12345678);
    for(unsigned n=0;n<256;++n)poll();
    const auto after_idle=api("CreateThread",{0,0,Start,scratch,0,0});CHECK(after_idle);poll();poll();
    CHECK(api("WaitForSingleObject",{after_idle,0})==0 && process.memory.load(scratch,32)==130);
    CHECK(api("CloseHandle",{after_idle})==1);poll();
    const auto report=process.state().reports.at("scheduler")();
    CHECK(report.find("\"idle_poll_skips\":")!=std::string::npos && report.find("\"idle_poll_skips\":0")==std::string::npos);
    std::ofstream output(argv[1]);output<<"{\"schema\":\"wa2.portable-scheduler.v1\",\"scheduler\":"<<report
        <<",\"sequential_thread_lifetimes\":130,\"idle_worker_restart_checked\":true,\"mutex_wait_resume\":true,\"abandoned_mutex\":true,\"cross_host_thread_signal\":true,"
          "\"tls_and_caller_state_preserved\":true,\"source_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"audio_device_opened\":false,\"passed\":true}\n";
    std::cout<<"PASS portable scheduler: waits, events, TLS, idle fast path and 130 complete thread lifetimes.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
