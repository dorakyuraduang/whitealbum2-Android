// Native compiled test functions exercise scheduler/API boundaries. This test
// dispatcher is not an instruction interpreter and is never linked into WA2.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <fstream>
#include <iostream>
#include "support.hpp"
#include "wa2_win32.hpp"
using wr::U32;
#define CHECK(v) do{if(!(v))throw std::runtime_error(#v);}while(false)
static wr::Process* process;
constexpr U32 Worker=0x00401000,PriorityWorker=Worker+0x100,Destructor=Worker+0x200,WaitWorker=Worker+0x300;
static bool step(wr::Cpu& c,wr::Memory& m,std::uint64_t&){
    auto call=[&](const char* name,std::initializer_list<U32> args,U32 ret){for(auto it=args.end();it!=args.begin();)wr::push(c,m,*--it);wr::push(c,m,ret);c.eip=process->resolve("kernel32.dll",name);CHECK(c.eip);};
    if(c.eip==Destructor){const auto context=m.load(c.r[wr::ESP]+4,32);m.store(context+36,m.load(context+36,32)+1,32);c.eip=wr::pop(c,m);c.r[wr::ESP]+=4;c.r[wr::EAX]=0;return true;}
    if(c.eip==PriorityWorker){
        const auto context=m.load(c.r[wr::ESP]+4,32),count=m.load(context,32)+1;m.store(context,count,32);
        if(count==100000){c.r[wr::EAX]=count;c.eip=wr::pop(c,m);c.r[wr::ESP]+=4;}return true;
    }
    if(c.eip==WaitWorker){c.r[wr::EBX]=m.load(c.r[wr::ESP]+4,32);const auto at=c.r[wr::EBX];call("WaitForMultipleObjects",{m.load(at,32),at+32,m.load(at+4,32),m.load(at+8,32)},WaitWorker+4);return true;}
    if(c.eip==WaitWorker+4){const auto at=c.r[wr::EBX];m.store(at+12,c.r[wr::EAX],32);m.store(at+16,process->last_error,32);m.store(at+20,1,32);c.r[wr::EBX]=0;c.r[wr::EAX]=88;c.eip=wr::pop(c,m);c.r[wr::ESP]+=4;return true;}
    const auto ctx=c.r[wr::EBX];switch(c.eip){
        case Worker:c.r[wr::EBX]=m.load(c.r[wr::ESP]+4,32);if(m.load(c.r[wr::EBX]+40,32)){call("ExitThread",{99},Worker+0x80);return true;}call("GetCurrentThreadId",{},Worker+4);return true;
        case Worker+4:m.store(ctx+16,c.r[wr::EAX],32);call("TlsGetValue",{m.load(ctx+4,32)},Worker+8);return true;
        case Worker+8:m.store(ctx+20,c.r[wr::EAX],32);call("FlsGetValue",{m.load(ctx+8,32)},Worker+12);return true;
        case Worker+12:m.store(ctx+24,c.r[wr::EAX],32);call("TlsSetValue",{m.load(ctx+4,32),0x4444},Worker+16);return true;
        case Worker+16:call("FlsSetValue",{m.load(ctx+8,32),ctx},Worker+20);return true;
        case Worker+20:c.fp.put16(0,0x027f);call("SetLastError",{0x1234},Worker+24);return true;
        case Worker+24:call("Sleep",{2},Worker+28);return true;
        case Worker+28:call("SetEvent",{m.load(ctx,32)},Worker+32);return true;
        case Worker+32:call("EnterCriticalSection",{m.load(ctx+12,32)},Worker+36);return true;
        case Worker+36:m.store(ctx+28,1,32);call("LeaveCriticalSection",{m.load(ctx+12,32)},Worker+40);return true;
        case Worker+40:c.r[wr::EAX]=77;c.r[wr::EBX]=0;c.eip=wr::pop(c,m);c.r[wr::ESP]+=4;return true;
        default:return false;
    }
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("usage: wa2_thread_tests original.exe");wr::ProcessOptions options;options.instruction_budget=1000000;
    wr::Process p(step,options);process=&p;wr::install_wa2_win32(p);p.load(wr::Image::load(argv[1]));
    // Worker PCs name synthetic C++ test functions, not the original PE's
    // instruction bodies. Native instruction equivalence has its own fixture.
    p.state().fast_step=nullptr;p.state().block_step=nullptr;
    auto call=[&](const char* name,std::initializer_list<U32> args){const auto target=p.resolve("kernel32.dll",name);CHECK(target);return p.callback(target,std::span(args.begin(),args.size()));};
    const auto buffer=p.allocate_bytes(4096),context=p.allocate_bytes(4096),critical=buffer+512;
    const auto tls=call("TlsAlloc",{}),fls=call("FlsAlloc",{Destructor});CHECK(tls!=0xffffffff && fls!=0xffffffff);
    CHECK(call("TlsSetValue",{tls,0x1111}));call("InitializeCriticalSection",{critical});call("EnterCriticalSection",{critical});
    const auto e=call("CreateEventA",{0,0,0,0});CHECK(e);wr::port::write(p,context,std::array<U32,4>{e,tls,fls,critical});
    const auto fp=p.cpu.fp.get16(0);const auto t=call("CreateThread",{0,0,Worker,context,0,buffer});CHECK(t && p.memory.load(buffer,32)!=1);
    call("SetLastError",{789});CHECK(call("WaitForSingleObject",{e,1000})==WAIT_OBJECT_0);CHECK(p.last_error==789);
    CHECK(p.cpu.fp.get16(0)==fp && call("GetCurrentThreadId",{})==1 && call("TlsGetValue",{tls})==0x1111 && call("FlsGetValue",{fls})==0);
    CHECK(p.memory.load(context+16,32)==p.memory.load(buffer,32) && p.memory.load(context+20,32)==0 && p.memory.load(context+24,32)==0);
    CHECK(call("WaitForSingleObject",{t,0})==WAIT_TIMEOUT && p.memory.load(context+28,32)==0);
    CHECK(call("WaitForSingleObject",{e,0})==WAIT_TIMEOUT);call("LeaveCriticalSection",{critical});
    CHECK(call("WaitForSingleObject",{t,1000})==WAIT_OBJECT_0);CHECK(call("GetExitCodeThread",{t,buffer+4}) && p.memory.load(buffer+4,32)==77);
    CHECK(p.memory.load(context+28,32)==1 && p.memory.load(context+36,32)==1 && p.state().critical_depth[critical]==0 && p.cpu.fp.get16(0)==fp);
    const auto name=p.put_string("thread-boundary-event");const auto manual=call("CreateEventA",{0,1,1,name});CHECK(manual);const auto alias=call("CreateEventA",{0,0,0,name});CHECK(alias && p.last_error==ERROR_ALREADY_EXISTS);
    CHECK(call("WaitForSingleObject",{manual,0})==0 && call("WaitForSingleObject",{alias,0})==0);CHECK(call("ResetEvent",{alias}));CHECK(call("WaitForSingleObject",{manual,0})==WAIT_TIMEOUT);
    CHECK(call("SetEvent",{manual}));CHECK(call("CloseHandle",{manual}));CHECK(call("WaitForSingleObject",{alias,0})==0);
    const auto context2=p.allocate_bytes(4096);p.memory.store(context2+40,1,32);const auto t2=call("CreateThread",{0,65536,Worker,context2,CREATE_SUSPENDED,buffer+8});CHECK(t2 && call("WaitForSingleObject",{t2,0})==WAIT_TIMEOUT);
    CHECK(call("SetThreadPriority",{t2,U32(THREAD_PRIORITY_ABOVE_NORMAL)}));CHECK(call("ResumeThread",{t2})==1);wr::port::write(p,buffer+16,std::array<U32,2>{t,t2});CHECK(call("WaitForMultipleObjects",{2,buffer+16,1,1000})==WAIT_OBJECT_0);
    CHECK(call("GetExitCodeThread",{t2,buffer+4}) && p.memory.load(buffer+4,32)==99);CHECK(call("WaitForMultipleObjects",{2,buffer+16,0,0})==WAIT_OBJECT_0);
    for(auto h:{e,alias,t,t2})CHECK(call("CloseHandle",{h}));CHECK(!call("SetEvent",{e}) && p.last_error==ERROR_INVALID_HANDLE);
    // Simultaneously runnable decoder/render-style workers must receive
    // different shares when the application raises the decoder's priority.
    // Also prove that a raised priority stays bounded and lets others run.
    std::array<U32,3> priority_contexts{},priority_threads{};
    for(unsigned n=0;n<3;++n){priority_contexts[n]=p.allocate_bytes(4096);priority_threads[n]=call("CreateThread",{0,65536,PriorityWorker,priority_contexts[n],0,0});CHECK(priority_threads[n]);}
    CHECK(call("SetThreadPriority",{priority_threads[0],U32(THREAD_PRIORITY_BELOW_NORMAL)}));
    CHECK(call("SetThreadPriority",{priority_threads[2],U32(THREAD_PRIORITY_HIGHEST)}));
    const auto main_cpu=p.cpu;
    for(auto& poll:p.state().poll)poll();
    const auto low=p.memory.load(priority_contexts[0],32),normal=p.memory.load(priority_contexts[1],32),high=p.memory.load(priority_contexts[2],32);
    CHECK(low>0 && low<normal && normal<high && high<100000);
    CHECK(p.cpu.r==main_cpu.r && p.cpu.eip==main_cpu.eip && p.state().current_thread_id==1);
    for(auto h:priority_threads){CHECK(call("WaitForSingleObject",{h,2000})==WAIT_OBJECT_0);CHECK(call("CloseHandle",{h}));}
    for(auto at:priority_contexts)CHECK(p.memory.load(at,32)==100000);
    auto poll=[&]{for(auto& fn:p.state().poll)fn();};
    auto start_wait=[&](std::span<const U32> handles,bool all,U32 timeout){
        const auto at=p.allocate_bytes(4096);wr::port::write(p,at,std::array<U32,3>{U32(handles.size()),U32(all),timeout});
        p.memory.copy_in(at+32,std::span(reinterpret_cast<const std::uint8_t*>(handles.data()),handles.size_bytes()));
        const auto thread=call("CreateThread",{0,65536,WaitWorker,at,0,0});CHECK(thread);poll();CHECK(!p.memory.load(at+20,32));return std::array<U32,2>{at,thread};
    };
    // A blocked API preserves its PC/stack and does not repeat dispatch or
    // exceptions on every instruction poll. Its auto-reset result is consumed
    // exactly once when the scheduler makes that thread runnable.
    const auto auto_event=call("CreateEventA",{0,0,0,0});CHECK(auto_event);
    const std::array<U32,1> auto_handles{auto_event};const auto auto_wait=start_wait(auto_handles,false,INFINITE);
    const auto main_wait_cpu=p.cpu;const auto calls_before=p.api_calls;
    for(unsigned n=0;n<2000;++n)poll();
    CHECK(p.api_calls==calls_before && !p.state().api_retry && !p.state().yield_requested && p.cpu.r==main_wait_cpu.r && p.cpu.eip==main_wait_cpu.eip);
    CHECK(call("SetEvent",{auto_event}));poll();
    CHECK(p.memory.load(auto_wait[0]+20,32)==1 && p.memory.load(auto_wait[0]+12,32)==WAIT_OBJECT_0);
    CHECK(call("WaitForSingleObject",{auto_event,0})==WAIT_TIMEOUT && call("WaitForSingleObject",{auto_wait[1],0})==WAIT_OBJECT_0);
    CHECK(call("CloseHandle",{auto_event}) && call("CloseHandle",{auto_wait[1]}));
    const auto all_a=call("CreateEventA",{0,1,1,0}),all_b=call("CreateEventA",{0,1,0,0});
    const std::array<U32,2> all_handles{all_a,all_b};const auto all_wait=start_wait(all_handles,true,INFINITE);poll();CHECK(!p.memory.load(all_wait[0]+20,32));
    CHECK(call("SetEvent",{all_b}));poll();CHECK(p.memory.load(all_wait[0]+20,32)==1 && p.memory.load(all_wait[0]+12,32)==WAIT_OBJECT_0);
    for(auto h:{all_a,all_b,all_wait[1]})CHECK(call("CloseHandle",{h}));
    const auto timeout_event=call("CreateEventA",{0,0,0,0});const std::array<U32,1> timeout_handles{timeout_event};const auto timeout_wait=start_wait(timeout_handles,false,2);
    call("Sleep",{5});poll();CHECK(p.memory.load(timeout_wait[0]+20,32)==1 && p.memory.load(timeout_wait[0]+12,32)==WAIT_TIMEOUT);
    const auto closed_wait=start_wait(timeout_handles,false,INFINITE);CHECK(call("CloseHandle",{timeout_event}));poll();
    CHECK(p.memory.load(closed_wait[0]+20,32)==1 && p.memory.load(closed_wait[0]+12,32)==WAIT_FAILED && p.memory.load(closed_wait[0]+16,32)==ERROR_INVALID_HANDLE);
    for(auto h:{timeout_wait[1],closed_wait[1]})CHECK(call("CloseHandle",{h}));
    // DirectSound signals its native event without calling the guest SetEvent
    // wrapper. A blocked logical worker must still observe and consume it once.
    const auto native_event=call("CreateEventA",{0,0,0,0});const std::array<U32,1> native_handles{native_event};const auto native_wait=start_wait(native_handles,false,INFINITE);
    CHECK(::SetEvent(reinterpret_cast<HANDLE>(p.state().events.at(native_event)->native)));
    for(unsigned n=0;n<100 && !p.memory.load(native_wait[0]+20,32);++n){poll();::Sleep(1);}
    CHECK(p.memory.load(native_wait[0]+20,32)==1 && p.memory.load(native_wait[0]+12,32)==WAIT_OBJECT_0);
    CHECK(call("WaitForSingleObject",{native_event,0})==WAIT_TIMEOUT);
    CHECK(call("CloseHandle",{native_event}) && call("CloseHandle",{native_wait[1]}));
    call("DeleteCriticalSection",{critical});CHECK(call("TlsFree",{tls}) && call("FlsFree",{fls}));
    const auto out=std::filesystem::absolute(argv[1]).parent_path()/"build/thread-tests";std::filesystem::create_directories(out);std::ofstream report(out/"report.json");report<<p.report()<<'\n';report.close();CHECK(report);
    std::cout<<"PASS logical threads: separate CPU/FPU/TEB/TLS/FLS/error state, auto/manual/named events, contended critical section, Sleep, suspended start, WaitAny/All, return/ExitThread, compiled FLS destructor, handle lifetime, bounded priority scheduling, blocked API continuation without exceptions, single-consumption auto-reset wake, finite timeout and closed wait handle\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
