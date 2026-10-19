#include "scheduler.hpp"
#include "../../port/guest_api.hpp"
#include <algorithm>
#include <bit>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>

namespace wa2::platform::compat {namespace {
using namespace wr;using wr::port::Args;
constexpr U32 ThreadReturn=0xffffffc0,Timeout=258,Failed=0xffffffff,Infinite=0xffffffff,StillActive=259;
struct Event {bool manual{},signaled{};};
struct Hub {std::mutex mutex;std::condition_variable changed;std::atomic<std::uint64_t> generation{};};
struct Thread {
    Cpu cpu;U32 id{},handle{},stack{},size{},teb{},error{},exit_code{StillActive},suspended{};
    std::map<U32,U32> tls;std::shared_ptr<Win32State::NativeEvent> completion;
    std::uint64_t wake{},deadline{};U32 wait_pc{},wait_sp{},result{},wait_error{},critical{};
    std::uint64_t next_probe{},observed_generation{};
    std::vector<U32> handles;bool done{},exiting{},waiting{},all{},ready{};int priority{};
};
class Scheduler {
    Process& p;
    std::shared_ptr<Hub> hub=std::make_shared<Hub>();
    std::map<U32,std::weak_ptr<Event>> event_objects;
    std::map<std::string,std::weak_ptr<Win32State::NativeEvent>> names;
    struct MutexInfo {std::weak_ptr<Win32State::Mutex> object;U32 owner{};bool abandoned{};};
    std::map<const Win32State::Mutex*,MutexInfo> mutexes;
    std::map<U32,std::shared_ptr<Thread>> threads;
    std::shared_ptr<Thread> active;
    U32 next_id{2};unsigned baseline{};int main_priority{};bool in_poll{},stopped{};
    std::uint64_t created{},completed{},switches{},waits{},yields{};
    std::uint64_t poll_calls{},idle_poll_skips{},idle_serial{};
    std::uint64_t &main_sleep_calls{p.host_work.sleep_calls},&main_sleep_requested_ms{p.host_work.sleep_requested_ms},&main_sleep_wall_ns{p.host_work.sleep_wall_ns},&main_wait_calls{p.host_work.wait_calls},&main_wait_wall_ns{p.host_work.wait_wall_ns};
    static std::uint64_t now(){return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    static std::uint64_t deadline(U32 ms){return ms==Infinite?std::numeric_limits<std::uint64_t>::max():now()+std::uint64_t(ms)*1000000;}
    struct WaitTimer {std::uint64_t& total;std::uint64_t start{now()};~WaitTimer(){total+=now()-start;}};
    [[noreturn]]void stop(const char* message){throw GuestFault(FaultKind::unsupported,p.cpu.eip,std::string("native scheduler: ")+message);}
    void bind(const char* name,unsigned count,std::function<U32(Process&,Args)> function){
        p.replace_api("kernel32.dll",name,count,[function=std::move(function),count](Process& q){
            const auto cached=q.argument_snapshot();if(cached.size()==count)return function(q,cached);
            std::array<U32,16> values{};for(unsigned i=0;i<count;++i)values[i]=q.argument(i);return function(q,std::span(values).first(count));
        });
    }
    U32 token(){const auto value=p.state().next_handle;if(value>=0xcf000000)stop("handle count limit");p.state().next_handle+=16;return value;}
    std::shared_ptr<Win32State::NativeEvent> event(bool manual,bool initial,U32 handle){
        auto state=std::make_shared<Event>(Event{manual,initial});auto object=std::make_shared<Win32State::NativeEvent>();
        object->signal=[state,notify=hub]{std::lock_guard guard(notify->mutex);state->signaled=true;++notify->generation;notify->changed.notify_all();};
        event_objects[handle]=state;return object;
    }
    std::shared_ptr<Event> lookup_event(U32 handle){
        if(!p.state().events.contains(handle))return {};
        const auto found=event_objects.find(handle);return found==event_objects.end()?std::shared_ptr<Event>{}:found->second.lock();
    }
    MutexInfo& mutex_info(const std::shared_ptr<Win32State::Mutex>& object){
        auto& info=mutexes[object.get()];if(info.object.expired())info={object,0,false};return info;
    }
    U32 probe(std::span<const U32> handles,bool all,U32 tid,U32& error){
        std::vector<std::shared_ptr<Event>> events;std::vector<std::shared_ptr<Win32State::Mutex>> locks;
        for(auto handle:handles){
            if(auto e=lookup_event(handle)){events.push_back(std::move(e));locks.push_back({});}
            else if(auto it=p.state().mutexes.find(handle);it!=p.state().mutexes.end()){events.push_back({});locks.push_back(it->second);}
            else{error=6;return Failed;}
        }
        std::lock_guard guard(hub->mutex);unsigned first=unsigned(handles.size());
        for(unsigned i=0;i<handles.size();++i){
            const bool ready=events[i]?events[i]->signaled:!locks[i]->depth || mutex_info(locks[i]).owner==tid;
            if(all && !ready)return Timeout;
            if(ready && first==handles.size())first=i;
        }
        if(first==handles.size())return Timeout;
        U32 result=all?0:first;bool abandoned=false;
        for(unsigned i=0;i<handles.size();++i)if(all || i==first){
            if(events[i]){if(!events[i]->manual)events[i]->signaled=false;}
            else{auto& info=mutex_info(locks[i]);if(info.abandoned && !abandoned){result=0x80+i;abandoned=true;}info.abandoned=false;info.owner=tid;++locks[i]->depth;}
        }
        return result;
    }
    void yield(bool retry){
        if(!active || p.state().callback_depth!=baseline)stop("cannot suspend inside a native callback");
        p.state().yield_requested=true;p.state().api_retry=retry;++yields;
    }
    bool runnable(Thread& t,std::uint64_t time){
        if(t.done || t.suspended || t.wake>time)return false;
        if(t.critical && p.state().critical_depth[t.critical] && p.state().critical_owner[t.critical]!=t.id)return false;
        if(t.waiting){
            const auto generation=hub->generation.load(std::memory_order_relaxed);
            if(time<t.deadline && time<t.next_probe && generation==t.observed_generation)return false;
            U32 error{};const auto result=probe(t.handles,t.all,t.id,error);
            t.next_probe=time+250000;t.observed_generation=generation;
            if(result==Timeout && time<t.deadline)return false;
            t.result=result;t.wait_error=error;t.waiting=false;t.ready=true;
        }
        t.critical=0;return true;
    }
    void finish(Thread& t){
        if(t.done)return;for(auto& cleanup:p.state().thread_exit)cleanup(t.id);
        for(auto& [object,info]:mutexes)if(info.owner==t.id){if(auto lock=info.object.lock())lock->depth=0;info.owner=0;info.abandoned=true;}
        t.done=true;t.exiting=false;t.completion->signal();++completed;
        if(t.stack){p.memory.release(t.stack);t.stack=0;}if(t.teb){p.memory.release(t.teb);t.teb=0;}
    }
    Thread* find(U32 handle){
        if(handle==0xfffffffe)return active.get();
        const auto found=threads.find(handle);if(found==threads.end() || !p.state().events.contains(handle))return nullptr;return found->second.get();
    }
public:
    explicit Scheduler(Process& process):p(process){}
    U32 create(Args a){
        if(a[0] || (a[4]&~0x10004u)){p.set_error(87);return 0;}
        // Idle housekeeping is amortized, but a newly created worker must
        // never see closed completed handles as exhausted live thread slots.
        if(threads.size()>=64)for(auto it=threads.begin();it!=threads.end();)if(it->second->done && !p.state().events.contains(it->first))it=threads.erase(it);else ++it;
        if(threads.size()>=64){p.set_error(164);return 0;}
        p.memory.check(a[2],1,Memory::Execute);if(a[5])p.memory.check(a[5],4,Memory::Write);
        const auto bytes=(std::max<std::uint64_t>(65536,a[1]?a[1]:1048576)+4095)&~4095ull;
        if(bytes>8*1024*1024){p.set_error(8);return 0;}
        auto t=std::make_shared<Thread>();t->handle=token();t->completion=event(true,false,t->handle);t->id=next_id++;t->size=U32(bytes);t->suspended=(a[4]&4)?1:0;
        try{
            t->stack=p.allocate_bytes(t->size);t->teb=p.allocate_bytes(4096);p.memory.protect(t->stack,4096,0);
            std::array<std::uint8_t,4096> teb{};p.memory.copy_out(p.cpu.fs_base,teb);p.memory.copy_in(t->teb,teb);
            t->cpu.fs_valid=true;t->cpu.fs_base=t->teb;t->cpu.eip=a[2];t->cpu.r[ESP]=t->stack+t->size;
            for(auto [offset,value]:std::array<std::pair<U32,U32>,7>{{{0,0xffffffff},{4,t->cpu.r[ESP]},{8,t->stack+4096},{0x18,t->teb},{0x20,1},{0x24,t->id},{0x34,0}}})p.memory.store(t->teb+offset,value,32);
            push(t->cpu,p.memory,a[3]);push(t->cpu,p.memory,ThreadReturn);
            p.state().events[t->handle]=t->completion;threads[t->handle]=t;
            if(a[5])p.memory.store(a[5],t->id,32);++created;return t->handle;
        }catch(...){if(t->stack)p.memory.release(t->stack);if(t->teb)p.memory.release(t->teb);event_objects.erase(t->handle);throw;}
    }
    void poll(){
        if(stopped || in_poll)return;
        ++poll_calls;
        // Active scheduling keeps every existing boundary. With no live workers,
        // only weak handle housekeeping remains; amortize it over 64 polls.
        if(created==completed && (++idle_serial&63)){++idle_poll_skips;return;}
        // Reap closed completed threads even when no remaining thread is
        // runnable. Otherwise repeated load/skip operations leak thread slots.
        for(auto it=threads.begin();it!=threads.end();)if(it->second->done && !p.state().events.contains(it->first))it=threads.erase(it);else ++it;
        for(auto it=event_objects.begin();it!=event_objects.end();)if(it->second.expired())it=event_objects.erase(it);else ++it;
        for(auto it=mutexes.begin();it!=mutexes.end();)if(it->second.object.expired())it=mutexes.erase(it);else ++it;
        if(threads.empty())return;
        std::vector<std::shared_ptr<Thread>> ready;const auto time=now();
        for(const auto& [handle,t]:threads)if(runnable(*t,time))ready.push_back(t);
        if(ready.empty())return;
        in_poll=true;const auto saved=p.cpu;const auto error=p.last_error,tid=p.state().current_thread_id;
        auto tls=std::move(p.state().tls);const auto prior_yield=p.state().yield_requested;
        auto restore=[&]{p.cpu=saved;p.state().tls=std::move(tls);p.state().current_thread_id=tid;p.set_error(error);p.state().yield_requested=prior_yield;active.reset();in_poll=false;};
        try{
            for(const auto& t:ready){
                if(p.exited())break;active=t;p.cpu=t->cpu;p.state().tls=std::move(t->tls);p.state().current_thread_id=t->id;p.set_error(t->error);p.state().yield_requested=false;baseline=p.state().callback_depth;++switches;
                p.state().execute_slice(ThreadReturn,t->priority>=2?16:t->priority<0?1:4);
                if(t->exiting || p.cpu.eip==ThreadReturn){
                    if(!t->exiting){if(p.cpu.r[ESP]!=t->stack+t->size)stop("thread procedure stack cleanup mismatch");t->exit_code=p.cpu.r[EAX];}finish(*t);
                }
                t->cpu=p.cpu;t->error=p.last_error;t->tls=std::move(p.state().tls);
            }
            for(auto it=threads.begin();it!=threads.end();)if(it->second->done && !p.state().events.contains(it->first))it=threads.erase(it);else ++it;
            for(auto it=event_objects.begin();it!=event_objects.end();)if(it->second.expired())it=event_objects.erase(it);else ++it;
            for(auto it=mutexes.begin();it!=mutexes.end();)if(it->second.object.expired())it=mutexes.erase(it);else ++it;
            restore();
        }catch(...){auto failure=std::current_exception();restore();std::rethrow_exception(failure);}
    }
    void sleep(U32 ms){
        if(active){active->wake=deadline(ms);yield(false);return;}
        ++main_sleep_calls;main_sleep_requested_ms+=ms;WaitTimer timing{main_sleep_wall_ns};
        if(!ms){++p.host_work.sleep_zero_calls;poll();std::this_thread::yield();return;}
        const auto until=deadline(ms);
        while(!p.exited()){
            for(auto& callback:p.state().poll)callback();const auto time=now();if(time>=until)return;
            std::unique_lock lock(hub->mutex);hub->changed.wait_for(lock,std::chrono::nanoseconds(std::min<std::uint64_t>(until-time,2000000)));
        }
    }
    U32 wait(std::span<const U32> handles,bool all,U32 ms){
        if(active && active->ready){
            if(active->wait_pc!=p.cpu.eip || active->wait_sp!=p.cpu.r[ESP])stop("resumed wait changed its call frame");
            active->ready=false;if(active->result==Failed)p.set_error(active->wait_error);return active->result;
        }
        if(handles.empty() || handles.size()>64 || std::set<U32>(handles.begin(),handles.end()).size()!=handles.size()){p.set_error(87);return Failed;}
        ++waits;U32 error{};auto result=probe(handles,all,p.state().current_thread_id,error);
        if(result==Failed){p.set_error(error);return result;}if(result!=Timeout || !ms)return result;
        if(active){active->handles.assign(handles.begin(),handles.end());active->all=all;active->deadline=deadline(ms);active->waiting=true;active->wait_pc=p.cpu.eip;active->wait_sp=p.cpu.r[ESP];yield(true);return Timeout;}
        ++main_wait_calls;WaitTimer timing{main_wait_wall_ns};
        const auto until=deadline(ms);
        while(!p.exited()){
            for(auto& callback:p.state().poll)callback();result=probe(handles,all,p.state().current_thread_id,error);
            if(result==Failed)p.set_error(error);if(result!=Timeout || now()>=until)return result;
            std::unique_lock lock(hub->mutex);hub->changed.wait_for(lock,std::chrono::milliseconds(1));
        }
        return Timeout;
    }
    void critical(U32 at){
        while(p.state().critical_depth[at] && p.state().critical_owner[at]!=p.state().current_thread_id){
            if(active){active->critical=at;yield(true);return;}for(auto& callback:p.state().poll)callback();
            if(p.exited())stop("process exited during critical-section wait");std::this_thread::yield();
        }
    }
    void shutdown() noexcept{if(stopped)return;stopped=true;for(auto& [handle,t]:threads)try{finish(*t);}catch(...){}threads.clear();}
    void install(){
        bind("CreateEventA",4,[this](Process&,Args a)->U32{
            if(a[0]){p.set_error(87);return 0;}const auto name=a[3]?p.read_string(a[3]):std::string();const auto handle=token();
            auto object=name.empty()?std::shared_ptr<Win32State::NativeEvent>{}:names[name].lock();
            if(object){
                auto existing=std::find_if(p.state().events.begin(),p.state().events.end(),[&](const auto& pair){return pair.second==object;});
                if(existing==p.state().events.end())stop("named event lost its live token");event_objects[handle]=event_objects[existing->first];p.set_error(183);
            }else{object=event(a[1]!=0,a[2]!=0,handle);if(!name.empty())names[name]=object;p.set_error(0);}
            p.state().events[handle]=object;return handle;
        });
        for(bool setting:{false,true})bind(setting?"SetEvent":"ResetEvent",1,[this,setting](Process&,Args a)->U32{
            const auto object=lookup_event(a[0]);if(!object || threads.contains(a[0])){p.set_error(6);return 0;}
            std::lock_guard lock(hub->mutex);object->signaled=setting;++hub->generation;hub->changed.notify_all();return 1;
        });
        bind("CreateMutexA",3,[this](Process&,Args a)->U32{
            if(a[0]){p.set_error(87);return 0;}const auto name=a[2]?p.read_string(a[2]):std::string();
            auto object=name.empty()?std::shared_ptr<Win32State::Mutex>{}:p.state().mutex_names[name].lock();
            if(object)p.set_error(183);else{object=std::make_shared<Win32State::Mutex>();object->name=name;object->depth=a[1]?1:0;mutexes[object.get()]={object,a[1]?p.state().current_thread_id:0,false};if(!name.empty())p.state().mutex_names[name]=object;p.set_error(0);}
            const auto handle=token();p.state().mutexes[handle]=object;return handle;
        });
        bind("ReleaseMutex",1,[this](Process&,Args a)->U32{
            const auto found=p.state().mutexes.find(a[0]);if(found==p.state().mutexes.end()){p.set_error(6);return 0;}auto& info=mutex_info(found->second);
            if(!found->second->depth || info.owner!=p.state().current_thread_id){p.set_error(288);return 0;}
            if(!--found->second->depth)info.owner=0;++hub->generation;hub->changed.notify_all();return 1;
        });
        bind("WaitForMultipleObjects",4,[this](Process&,Args a)->U32{if(!a[0] || a[0]>64){p.set_error(87);return Failed;}std::vector<U32> handles(a[0]);p.memory.copy_out(a[1],{reinterpret_cast<std::uint8_t*>(handles.data()),handles.size()*4});return wait(handles,a[2]!=0,a[3]);});
        bind("CreateThread",6,[this](Process&,Args a){return create(a);});
        bind("ExitThread",1,[this](Process&,Args a)->U32{if(!active){p.exit(a[0]);return 0;}active->exit_code=a[0];active->exiting=true;yield(false);return 0;});
        bind("Sleep",1,[this](Process&,Args a)->U32{sleep(a[0]);return 0;});
        bind("SetThreadPriority",2,[this](Process&,Args a)->U32{
            const auto value=std::bit_cast<std::int32_t>(a[1]);if((value<-2 || value>2) && value!=-15 && value!=15){p.set_error(87);return 0;}
            if(a[0]==0xfffffffe && !active){main_priority=value;return 1;}auto* thread=find(a[0]);if(!thread){p.set_error(6);return 0;}thread->priority=value;return 1;
        });
        bind("GetThreadPriority",1,[this](Process&,Args a)->U32{if(a[0]==0xfffffffe && !active)return U32(main_priority);if(const auto* t=find(a[0]))return U32(t->priority);p.set_error(6);return 0x7fffffff;});
        bind("GetExitCodeThread",2,[this](Process&,Args a)->U32{auto* t=find(a[0]);if(!t){p.set_error(6);return 0;}p.memory.store(a[1],t->exit_code,32);return 1;});
        for(bool resume:{false,true})bind(resume?"ResumeThread":"SuspendThread",1,[this,resume](Process&,Args a)->U32{auto* t=find(a[0]);if(!t){p.set_error(6);return Failed;}const auto before=t->suspended;if(resume){if(before)--t->suspended;}else{if(before==127){p.set_error(8);return Failed;}++t->suspended;if(t==active.get())yield(false);}return before;});
        p.state().wait_objects=[this](auto handles,bool all,U32 ms){return wait(handles,all,ms);};
        p.state().sleep_thread=[this](U32 ms){sleep(ms);};p.state().wait_critical=[this](U32 at){critical(at);};
    }
    std::string report()const{return "{\"backend\":\"portable-compiled-functions\",\"created\":"+std::to_string(created)+",\"completed\":"+std::to_string(completed)+",\"switches\":"+std::to_string(switches)+",\"waits\":"+std::to_string(waits)+",\"yields\":"+std::to_string(yields)+
        ",\"poll_calls\":"+std::to_string(poll_calls)+",\"idle_poll_skips\":"+std::to_string(idle_poll_skips)+",\"main_sleep_calls\":"+std::to_string(main_sleep_calls)+",\"main_sleep_requested_ms\":"+std::to_string(main_sleep_requested_ms)+",\"main_sleep_wall_ns\":"+std::to_string(main_sleep_wall_ns)+",\"main_wait_calls\":"+std::to_string(main_wait_calls)+",\"main_wait_wall_ns\":"+std::to_string(main_wait_wall_ns)+"}";}
};
}
void install_scheduler(wr::Process& process){
    if(!process.function_execution())throw std::logic_error("portable scheduler requires static function execution");
    auto state=std::make_shared<Scheduler>(process);state->install();
    process.state().poll.push_back([state]{state->poll();});process.state().shutdown.push_back([state]{state->shutdown();});
    process.state().reports["scheduler"]=[state]{return state->report();};
}
}
