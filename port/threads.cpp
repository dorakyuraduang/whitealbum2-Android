// Cooperative logical Windows threads execute only statically generated guest
// instructions. Host APIs and graphics remain on their owning native thread.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <limits>
#include <sstream>
#include "support.hpp"
#include "wa2_win32.hpp"
namespace wr { namespace {
using namespace port;
constexpr U32 ThreadReturn=0xffffffc0;
struct Thread {
    Cpu cpu;U32 id{},handle{},stack{},size{},teb{},error{},exit_code{STILL_ACTIVE},suspended{};
    std::map<U32,U32> tls;std::shared_ptr<Win32State::NativeEvent> completion;
    std::uint64_t wake{},deadline{};U32 wait_pc{},wait_sp{};bool done{},exiting{};
    std::vector<U32> wait_handles;std::vector<HANDLE> wait_native;
    U32 wait_result{},wait_error{},critical_wait{};bool waiting{},wait_all{},wait_ready{};
    std::uint64_t next_probe{},observed_signals{};
    int priority{};
};
class Scheduler {
    Process& p;std::map<U32,std::shared_ptr<Thread>> threads;
    std::map<std::string,std::weak_ptr<Win32State::NativeEvent>> names;
    std::shared_ptr<Thread> active;U32 next_id{2};bool in_poll{},stopped{};unsigned callback_baseline{};
    std::uint64_t created{},completed{},switches{},waits{},events{},wait_probes{},blocked_polls{},api_retries{};
    std::uint64_t signal_generation{};
    std::uint64_t main_priority_calls{};int requested_main_priority{};
    HANDLE sleep_timer{};bool high_resolution_timer{};std::uint64_t timer_waits{},timer_failures{};
    bool native_short_sleep{[]{wchar_t value[2]{};return ::GetEnvironmentVariableW(L"WA2_NATIVE_SLEEP",value,2)==1 && value[0]==L'1';}()};
    struct SleepTiming {std::uint64_t calls{},requested_ms{},total_ns{},os_ns{},worker_ns{};};
    std::map<U32,SleepTiming> sleep_timing;
    SleepTiming main_sleep;
    std::uint64_t os_sleep_calls{};
    std::uint64_t worker_ns{};
    static std::uint64_t now_ns(){return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    static std::uint64_t deadline_ns(U32 ms){return ms==INFINITE?std::numeric_limits<std::uint64_t>::max():now_ns()+std::uint64_t(ms)*1000000;}
    void pause_ns(std::uint64_t ns){
        if(sleep_timer){LARGE_INTEGER due{};due.QuadPart=-LONGLONG((ns+99)/100);
            if(::SetWaitableTimer(sleep_timer,&due,0,nullptr,nullptr,FALSE) && ::WaitForSingleObject(sleep_timer,INFINITE)==WAIT_OBJECT_0){++timer_waits;return;}
            ++timer_failures;::CancelWaitableTimer(sleep_timer);
        }
        ::Sleep(DWORD(std::max<std::uint64_t>(1,(ns+999999)/1000000)));
    }
    static unsigned quantum(int priority){
        // The game's streaming decoder requests HIGHEST. Giving it the same
        // instruction share as rendering makes a one-second PCM refill miss
        // its deadline. Keep slices bounded while honoring that request.
        if(priority>=THREAD_PRIORITY_TIME_CRITICAL)return 262144;
        if(priority>=THREAD_PRIORITY_HIGHEST)return 65536;
        if(priority>=THREAD_PRIORITY_ABOVE_NORMAL)return 16384;
        if(priority<=THREAD_PRIORITY_IDLE)return 256;
        if(priority<=THREAD_PRIORITY_LOWEST)return 1024;
        if(priority<=THREAD_PRIORITY_BELOW_NORMAL)return 2048;
        return 4096;
    }
    [[noreturn]]void stop(const std::string& s){throw GuestFault(FaultKind::unsupported,p.cpu.eip,"guest scheduler: "+s);}
    void yield(){if(!active || p.state().callback_depth!=callback_baseline)stop("cannot suspend inside a native callback");p.state().yield_requested=true;p.state().api_retry=true;++api_retries;}
    bool runnable(Thread& t,std::uint64_t now){
        if(t.done || t.suspended || t.wake>now)return false;
        if(t.critical_wait && p.state().critical_depth[t.critical_wait] && p.state().critical_owner[t.critical_wait]!=t.id){++blocked_polls;return false;}
        if(t.waiting){
            U32 result=WAIT_TIMEOUT,error=0;
            for(auto handle:t.wait_handles)if(!p.state().events.contains(handle)){result=WAIT_FAILED;error=ERROR_INVALID_HANDLE;break;}
            // Native audio can signal independently of this scheduler. Probe
            // it at most every 250 us, while guest Set/ResetEvent invalidates
            // the delay immediately. Avoid a kernel wait every 4096 guest ops.
            if(result!=WAIT_FAILED && now<t.deadline && now<t.next_probe && t.observed_signals==signal_generation){++blocked_polls;return false;}
            if(result!=WAIT_FAILED){++wait_probes;result=::WaitForMultipleObjects(DWORD(t.wait_native.size()),t.wait_native.data(),t.wait_all,FALSE);if(result==WAIT_FAILED)error=::GetLastError();}
            t.next_probe=now+250000;t.observed_signals=signal_generation;
            if(result==WAIT_TIMEOUT && now<t.deadline){++blocked_polls;return false;}
            // An auto-reset event is consumed by this probe. Deliver its
            // cached result once, rather than issuing a second native wait.
            t.wait_result=result;t.wait_error=error;t.wait_ready=true;t.waiting=false;
        }
        t.critical_wait=0;return true;
    }
    U32 token(){const auto at=p.state().next_handle;if(at>=0xcf000000)stop("handle limit");p.state().next_handle+=16;return at;}
    std::shared_ptr<Win32State::NativeEvent> event(bool manual,bool initial){
        auto handle=::CreateEventW(nullptr,manual,initial,nullptr);if(!handle){p.set_error(::GetLastError());return {};}
        auto result=std::make_shared<Win32State::NativeEvent>();result->native=reinterpret_cast<std::uintptr_t>(handle);result->close=[handle]{::CloseHandle(handle);};result->signal=[handle]{::SetEvent(handle);};return result;
    }
    void finish(Thread& t){
        if(t.done)return;for(auto& cleanup:p.state().thread_exit)cleanup(t.id);
        t.done=true;t.exiting=false;::SetEvent(reinterpret_cast<HANDLE>(t.completion->native));++signal_generation;++completed;
        if(t.stack){p.memory.release(t.stack);t.stack=0;}if(t.teb){p.memory.release(t.teb);t.teb=0;}
    }
    Thread& find(U32 handle){
        if(handle==0xfffffffeu){if(!active)stop("main thread control is outside the worker profile");return *active;}
        auto it=threads.find(handle);if(it==threads.end() || !p.state().events.contains(handle))stop("invalid or closed thread handle");return *it->second;
    }
public:
    explicit Scheduler(Process& q):p(q){
        // The high-resolution flag is supported from Windows 10 1803. Older
        // systems use an ordinary owned timer, then Sleep if creation fails.
        sleep_timer=::CreateWaitableTimerExW(nullptr,nullptr,0x00000002,TIMER_MODIFY_STATE|SYNCHRONIZE);high_resolution_timer=sleep_timer!=nullptr;
        if(!sleep_timer)sleep_timer=::CreateWaitableTimerExW(nullptr,nullptr,0,TIMER_MODIFY_STATE|SYNCHRONIZE);
    }
    U32 create(Args a){
        if(a[0] || a[4]&~(CREATE_SUSPENDED|STACK_SIZE_PARAM_IS_A_RESERVATION)){p.set_error(ERROR_INVALID_PARAMETER);return 0;}
        if(threads.size()>=64){p.set_error(ERROR_MAX_THRDS_REACHED);return 0;}if(a[5])p.memory.check(a[5],4,Memory::Write);p.memory.check(a[2],1,Memory::Execute);
        const std::uint64_t size=(std::max(65536ull,a[1]?std::uint64_t(a[1]):1048576ull)+4095)&~4095ull;if(size>8*1024*1024){p.set_error(ERROR_NOT_ENOUGH_MEMORY);return 0;}
        auto t=std::make_shared<Thread>();t->completion=event(true,false);if(!t->completion)return 0;t->id=next_id++;t->size=U32(size);t->suspended=(a[4]&CREATE_SUSPENDED)?1:0;
        try{t->stack=p.allocate_bytes(t->size);t->teb=p.allocate_bytes(4096);p.memory.protect(t->stack,4096,0);std::array<std::uint8_t,4096> teb{};p.memory.copy_out(p.cpu.fs_base,teb);p.memory.copy_in(t->teb,teb);
            t->cpu.fs_valid=true;t->cpu.fs_base=t->teb;t->cpu.eip=a[2];t->cpu.r[ESP]=t->stack+t->size;
            p.memory.store(t->teb,0xffffffffu,32);p.memory.store(t->teb+4,t->cpu.r[ESP],32);p.memory.store(t->teb+8,t->stack+4096,32);p.memory.store(t->teb+0x18,t->teb,32);p.memory.store(t->teb+0x24,t->id,32);p.memory.store(t->teb+0x34,0,32);
            push(t->cpu,p.memory,a[3]);push(t->cpu,p.memory,ThreadReturn);t->handle=token();p.state().events[t->handle]=t->completion;threads[t->handle]=t;if(a[5])p.memory.store(a[5],t->id,32);++created;return t->handle;
        }catch(...){if(t->stack)p.memory.release(t->stack);if(t->teb)p.memory.release(t->teb);throw;}
    }
    void poll(){
        if(stopped || in_poll || threads.empty())return;
        std::vector<std::shared_ptr<Thread>> ready;const auto now=now_ns();
        for(const auto& [h,t]:threads)if(runnable(*t,now))ready.push_back(t);
        if(!p.options.profile_execution && p.state().fast_step)p.state().main_poll_interval=ready.empty()?131072:8192;
        if(ready.empty())return;
        in_poll=true;
        const auto saved=p.cpu;const auto error=p.last_error,tid=p.state().current_thread_id;auto tls=std::move(p.state().tls);const auto prior_yield=p.state().yield_requested;
        auto restore=[&]{p.cpu=saved;p.state().tls=std::move(tls);p.state().current_thread_id=tid;p.set_error(error);p.state().yield_requested=prior_yield;active.reset();in_poll=false;};
        try{
            for(const auto& t:ready){if(p.exited())break;active=t;p.cpu=t->cpu;p.state().tls=std::move(t->tls);p.state().current_thread_id=t->id;p.set_error(t->error);p.state().yield_requested=false;callback_baseline=p.state().callback_depth;++switches;
                const auto slice_started=p.options.profile_execution?now_ns():0;
                p.state().execute_slice(ThreadReturn,quantum(t->priority));
                if(slice_started)worker_ns+=now_ns()-slice_started;
                if(t->exiting || p.cpu.eip==ThreadReturn){if(!t->exiting){if(p.cpu.r[ESP]!=t->stack+t->size)stop("thread procedure stack cleanup mismatch");t->exit_code=p.cpu.r[EAX];}finish(*t);}
                t->cpu=p.cpu;t->error=p.last_error;t->tls=std::move(p.state().tls);
            }
            for(auto it=threads.begin();it!=threads.end();)if(it->second->done && !p.state().events.contains(it->first))it=threads.erase(it);else ++it;restore();
        }catch(...){auto failure=std::current_exception();restore();std::rethrow_exception(failure);}
    }
    void sleep(U32 ms){
        if(active){active->wake=deadline_ns(ms);p.state().yield_requested=true;return;}
        const bool timed=p.options.profile_execution;const auto started=now_ns(),worker_before=worker_ns;
        ++main_sleep.calls;main_sleep.requested_ms+=ms;
        auto* timing=timed?&sleep_timing[p.memory.load(p.cpu.r[ESP],32)]:nullptr;
        struct Finish {SleepTiming* timing;std::uint64_t started,before;Scheduler& self;
            ~Finish(){const auto elapsed=now_ns()-started;self.main_sleep.total_ns+=elapsed;if(timing){timing->total_ns+=elapsed;timing->worker_ns+=self.worker_ns-before;}}
        } finish{timing,started,worker_before,*this};
        if(timing){++timing->calls;timing->requested_ms+=ms;}
        auto host_sleep=[&](DWORD value){const auto at=now_ns();::Sleep(value);const auto elapsed=now_ns()-at;++os_sleep_calls;main_sleep.os_ns+=elapsed;if(timing)timing->os_ns+=elapsed;};
        auto host_wait=[&](std::uint64_t ns){const auto at=now_ns();pause_ns(ns);const auto elapsed=now_ns()-at;++os_sleep_calls;main_sleep.os_ns+=elapsed;if(timing)timing->os_ns+=elapsed;};
        if(!ms){poll();if(!p.exited())host_sleep(0);return;}
        // Diagnostic comparison against the exact native Win32 primitive.
        // Keep each OS wait request bounded to two milliseconds for audio.
        if(native_short_sleep && ms<=2){poll();if(!p.exited())host_sleep(ms);return;}
        if(threads.empty() && ms==INFINITE){host_sleep(ms);return;}
        const auto until=deadline_ns(ms);
        do{poll();if(p.exited())return;const auto now=now_ns();if(now>=until)return;
            // Keep servicing asynchronous decoder events while the main
            // thread sleeps, even for a long application-requested delay.
            host_wait(threads.empty()?until-now:std::min<std::uint64_t>(until-now,2000000));
        }while(true);
    }
    U32 wait(std::span<const U32> handles,bool all,U32 ms){
        if(active && active->wait_ready){
            if(active->wait_pc!=p.cpu.eip || active->wait_sp!=p.cpu.r[ESP])stop("resumed wait API changed its stack or PC");
            active->wait_ready=false;active->wait_pc=0;if(active->wait_result==WAIT_FAILED)p.set_error(active->wait_error);return active->wait_result;
        }
        if(handles.empty() || handles.size()>MAXIMUM_WAIT_OBJECTS){p.set_error(ERROR_INVALID_PARAMETER);return WAIT_FAILED;}std::vector<HANDLE> native;std::set<U32> unique;
        for(auto h:handles){if(!unique.insert(h).second){p.set_error(ERROR_INVALID_PARAMETER);return WAIT_FAILED;}if(auto it=p.state().events.find(h);it!=p.state().events.end())native.push_back(reinterpret_cast<HANDLE>(it->second->native));else if(auto it=p.state().mutexes.find(h);it!=p.state().mutexes.end()){if(active)stop("worker mutex ownership is outside the event/thread wait profile");native.push_back(reinterpret_cast<HANDLE>(it->second->native));}else{p.set_error(ERROR_INVALID_HANDLE);return WAIT_FAILED;}}
        ++waits;auto probe=[&]{auto r=::WaitForMultipleObjects(DWORD(native.size()),native.data(),all,FALSE);if(r==WAIT_FAILED)p.set_error(::GetLastError());return U32(r);};
        const auto deadline=deadline_ns(ms);
        if(active){auto result=probe();if(result!=WAIT_TIMEOUT || !ms){active->wait_pc=0;return result;}
            active->wait_pc=p.cpu.eip;active->wait_sp=p.cpu.r[ESP];active->deadline=deadline;
            active->wait_handles.assign(handles.begin(),handles.end());active->wait_native=std::move(native);active->wait_all=all;active->waiting=true;
            active->next_probe=now_ns()+250000;active->observed_signals=signal_generation;
            yield();return WAIT_TIMEOUT;
        }
        for(;;){auto result=probe();if(result!=WAIT_TIMEOUT || !ms)return result;poll();if(p.exited())return WAIT_TIMEOUT;if(now_ns()>=deadline)return WAIT_TIMEOUT;::Sleep(1);}
    }
    void critical(U32 at){
        while(p.state().critical_depth[at] && p.state().critical_owner[at]!=p.state().current_thread_id){if(active){active->critical_wait=at;yield();return;}poll();if(p.exited())stop("process exited during a critical-section wait");::Sleep(0);}
    }
    void install(){
        api(p,"kernel32.dll","CreateEventA",4,[this](Process&,Args a)->U32{if(a[0])stop("event security descriptor");std::string name=a[3]?p.read_string(a[3]):std::string();auto e=name.empty()?std::shared_ptr<Win32State::NativeEvent>{}:names[name].lock();if(e)p.set_error(ERROR_ALREADY_EXISTS);else{e=event(a[1]!=0,a[2]!=0);if(!e)return 0;if(!name.empty())names[name]=e;p.set_error(0);++events;}const auto at=token();p.state().events[at]=e;return at;});
        for(bool setting:{false,true})api(p,"kernel32.dll",setting?"SetEvent":"ResetEvent",1,[this,setting](Process&,Args a)->U32{auto it=p.state().events.find(a[0]);if(it==p.state().events.end() || threads.contains(a[0])){p.set_error(ERROR_INVALID_HANDLE);return 0;}const auto result=boolean(p,setting?::SetEvent(reinterpret_cast<HANDLE>(it->second->native)): ::ResetEvent(reinterpret_cast<HANDLE>(it->second->native)));if(result)++signal_generation;return result;});
        api(p,"kernel32.dll","WaitForMultipleObjects",4,[this](Process&,Args a){if(!a[0] || a[0]>64){p.set_error(ERROR_INVALID_PARAMETER);return U32(WAIT_FAILED);}std::vector<U32> handles(a[0]);p.memory.copy_out(a[1],std::span(reinterpret_cast<std::uint8_t*>(handles.data()),handles.size()*4));return wait(handles,a[2]!=0,a[3]);});
        api(p,"kernel32.dll","CreateThread",6,[this](Process&,Args a){return create(a);});
        api(p,"kernel32.dll","ExitThread",1,[this](Process&,Args a)->U32{if(!active)stop("ExitThread on the main thread");active->exiting=true;active->exit_code=a[0];p.state().yield_requested=true;return 0;});
        api(p,"kernel32.dll","GetExitCodeThread",2,[this](Process&,Args a){p.memory.check(a[1],4,Memory::Write);if(a[0]==0xfffffffe && !active){p.memory.store(a[1],STILL_ACTIVE,32);return U32(1);}auto& t=find(a[0]);p.memory.store(a[1],t.done?t.exit_code:STILL_ACTIVE,32);return U32(1);});
        api(p,"kernel32.dll","SetThreadPriority",2,[this](Process&,Args a)->U32{const auto priority=std::bit_cast<std::int32_t>(a[1]);if(priority!=-15 && priority!=15 && (priority< -2 || priority>2)){p.set_error(ERROR_INVALID_PARAMETER);return 0;}if(a[0]==0xfffffffe && !active){++main_priority_calls;requested_main_priority=priority;return 1;}find(a[0]).priority=priority;return 1;});
        api(p,"kernel32.dll","ResumeThread",1,[this](Process&,Args a){auto& t=find(a[0]);const auto old=t.suspended;if(old)--t.suspended;return old;});
        api(p,"kernel32.dll","SuspendThread",1,[this](Process&,Args a){auto& t=find(a[0]);const auto old=t.suspended++;if(active.get()==&t)p.state().yield_requested=true;return old;});
        api(p,"kernel32.dll","TryEnterCriticalSection",1,[this](Process&,Args a)->U32{const auto at=a[0];if(!p.state().critical_sections.contains(at))stop("uninitialized critical section");if(p.state().critical_depth[at] && p.state().critical_owner[at]!=p.state().current_thread_id)return 0;auto& depth=p.state().critical_depth[at];++depth;p.state().critical_owner[at]=p.state().current_thread_id;p.memory.store(at+4,depth-1,32);p.memory.store(at+8,depth,32);p.memory.store(at+12,p.state().current_thread_id,32);return 1;});
    }
    void shutdown() noexcept {if(stopped)return;stopped=true;for(auto& [id,t]:threads){if(t->stack)try{p.memory.release(t->stack);}catch(...){}if(t->teb)try{p.memory.release(t->teb);}catch(...){}}threads.clear();if(sleep_timer){::CancelWaitableTimer(sleep_timer);::CloseHandle(sleep_timer);sleep_timer=nullptr;}}
    std::string timer_report() const{return "{\"high_resolution\":"+std::string(high_resolution_timer?"true":"false")+",\"native_short_sleep\":"+std::string(native_short_sleep?"true":"false")+",\"waits\":"+std::to_string(timer_waits)+",\"failures\":"+std::to_string(timer_failures)+"}";}
    std::string report() const {
        std::ostringstream out;out<<"{\"backend\":\"cooperative-static-guest\",\"threads_created\":"<<created<<",\"threads_completed\":"<<completed<<",\"context_switches\":"<<switches<<",\"wait_calls\":"<<waits<<",\"events_created\":"<<events<<",\"main_priority_calls\":"<<main_priority_calls<<",\"requested_main_priority\":"<<requested_main_priority<<",\"native_main_priority\":"<<::GetThreadPriority(::GetCurrentThread())<<",\"workers\":[";
        bool first=true;for(const auto& [handle,t]:threads){if(!first)out<<',';first=false;out<<"{\"id\":"<<t->id<<",\"handle\":"<<handle<<",\"priority\":"<<t->priority<<",\"pc\":"<<t->cpu.eip<<",\"wait_pc\":"<<t->wait_pc<<",\"wake\":"<<t->wake<<",\"done\":"<<(t->done?"true":"false")<<",\"suspended\":"<<t->suspended<<",\"registers\":[";for(unsigned n=0;n<8;++n){if(n)out<<',';out<<t->cpu.r[n];}out<<"]}";}out<<"],\"wait_probes\":"<<wait_probes<<",\"blocked_polls\":"<<blocked_polls<<",\"api_retries\":"<<api_retries<<",\"profile_worker_ms\":"<<double(worker_ns)/1e6<<",\"sleep_profile\":[";
        first=true;for(const auto& [pc,t]:sleep_timing){if(!first)out<<',';first=false;out<<"{\"caller\":"<<pc<<",\"calls\":"<<t.calls<<",\"requested_ms\":"<<t.requested_ms<<",\"total_ms\":"<<double(t.total_ns)/1e6<<",\"os_sleep_ms\":"<<double(t.os_ns)/1e6<<",\"worker_ms\":"<<double(t.worker_ns)/1e6<<'}';}out<<"],\"main_sleep\":{\"calls\":"<<main_sleep.calls<<",\"requested_ms\":"<<main_sleep.requested_ms<<",\"elapsed_ms\":"<<double(main_sleep.total_ns)/1e6<<",\"os_calls\":"<<os_sleep_calls<<",\"os_elapsed_ms\":"<<double(main_sleep.os_ns)/1e6<<"}}";return out.str();
    }
};
} // namespace
void install_wa2_threads(Process& p){
    auto s=std::make_shared<Scheduler>(p);s->install();p.state().poll.push_back([s]{s->poll();});p.state().sleep_thread=[s](U32 ms){s->sleep(ms);};p.state().wait_objects=[s](std::span<const U32> h,bool all,U32 ms){return s->wait(h,all,ms);};p.state().wait_critical=[s](U32 at){s->critical(at);};p.state().shutdown.push_back([s]{s->shutdown();});p.state().reports["threads"]=[s]{return s->report();};p.state().reports["scheduler_timer"]=[s]{return s->timer_report();};
}
} // namespace wr
