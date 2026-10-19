// Check the native scheduler's wait deadlines without a window or screen access.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include "wa2_win32.hpp"
#include "support.hpp"
using wr::U32;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
static bool unused_step(wr::Cpu&,wr::Memory&,std::uint64_t&){return false;}
int main(int argc,char** argv){try{
    CHECK(argc==3);wr::ProcessOptions options;options.instruction_budget=1000000;options.profile_execution=true;
    wr::Process p(unused_step,options);wr::install_wa2_win32(p);p.load(wr::Image::load(argv[1]));
    p.state().fast_step=nullptr;p.state().block_step=nullptr;
    auto call=[&](const char* dll,const char* name,std::initializer_list<U32> args){
        const auto pc=p.resolve(dll,name);CHECK(pc);return p.callback(pc,std::span(args.begin(),args.size()));
    };
    CHECK(call("winmm.dll","timeBeginPeriod",{1})==0);
    const auto event=call("kernel32.dll","CreateEventA",{0,0,0,0});CHECK(event);
    const auto context=p.allocate_bytes(4096);
    // A suspended worker keeps the scheduler active without introducing work
    // into the interval. The old coarse-clock loop adds 8..16 ms here.
    const auto worker=call("kernel32.dll","CreateThread",{0,65536,0x401000,0,CREATE_SUSPENDED,context});CHECK(worker);
    auto measure=[](auto work){const auto at=std::chrono::steady_clock::now();work();return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-at).count();};
    const auto native=measure([]{for(unsigned n=0;n<64;++n)::Sleep(2);});
    const auto bridged=measure([&]{for(unsigned n=0;n<64;++n)call("kernel32.dll","Sleep",{2});});
    CHECK(bridged>=128.0); // A monotonic deadline must never finish early.
    const auto timeout=measure([&]{for(unsigned n=0;n<16;++n)CHECK(call("kernel32.dll","WaitForSingleObject",{event,2})==WAIT_TIMEOUT);});
    CHECK(timeout>=32.0);
    CHECK(call("kernel32.dll","SetEvent",{event}));
    CHECK(call("kernel32.dll","WaitForSingleObject",{event,0})==WAIT_OBJECT_0);
    CHECK(call("kernel32.dll","WaitForSingleObject",{event,0})==WAIT_TIMEOUT);
    CHECK(call("kernel32.dll","CloseHandle",{event}));CHECK(call("kernel32.dll","CloseHandle",{worker}));
    CHECK(call("winmm.dll","timeEndPeriod",{1})==0);
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.scheduler-timing.v1\",\"native_sleep_64x2ms\":"<<native
        <<",\"bridged_sleep_64x2ms\":"<<bridged<<",\"wait_16x2ms\":"<<timeout
        <<",\"deadline_never_early\":true,\"screen_access\":false,\"passed\":true}\n";CHECK(report);
    std::cout<<"PASS monotonic wait deadlines; native/bridged Sleep(2) x64: "<<native<<" / "<<bridged<<" ms; finite event waits: "<<timeout<<" ms\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
