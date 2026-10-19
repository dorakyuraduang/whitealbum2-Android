#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <winrecomp/resources.hpp>
#include "support.hpp"
namespace wr { namespace {
using namespace port;
struct LocaleCall {Process* process;U32 callback;std::exception_ptr failure;};
thread_local LocaleCall* locale_call{};
BOOL CALLBACK locale_callback(LPSTR value) noexcept {
    auto& call=*locale_call;U32 text=0;
    try{text=call.process->put_string(value);const std::array<U32,1> args{text};const auto result=call.process->callback(call.callback,args);call.process->memory.release(text);return result!=0;}
    catch(...){if(text)try{call.process->memory.release(text);}catch(...){}call.failure=std::current_exception();return FALSE;}
}
ResourceName resource_name(Process& p,U32 value){
    if(value<65536)return std::uint16_t(value);const auto name=p.read_string(value);
    const auto count=::MultiByteToWideChar(CP_ACP,0,name.data(),int(name.size()),nullptr,0);std::u16string wide(count,u'\0');
    if(count)::MultiByteToWideChar(CP_ACP,0,name.data(),int(name.size()),reinterpret_cast<wchar_t*>(wide.data()),count);return wide;
}
struct ResourceState {struct Item {Resource resource;U32 data{};};std::map<U32,Item> items;U32 next{0xba000000};};
struct ControlJob {DWORD code;std::atomic<bool> handled{false};HANDLE done{::CreateEventW(nullptr,TRUE,FALSE,nullptr)};~ControlJob(){if(done)::CloseHandle(done);}};
struct ControlState {Process* process;std::mutex mutex;std::deque<std::shared_ptr<ControlJob>> jobs;std::vector<U32> handlers;bool registered{},stopped{};};
std::mutex control_mutex;
std::weak_ptr<ControlState> control_state;
BOOL WINAPI control_callback(DWORD code) noexcept {
    try{std::shared_ptr<ControlState> state;{std::lock_guard lock(control_mutex);state=control_state.lock();}
    if(!state)return FALSE;auto job=std::make_shared<ControlJob>();job->code=code;if(!job->done)return FALSE;
    {std::lock_guard lock(state->mutex);if(state->stopped || state->handlers.empty())return FALSE;state->jobs.push_back(job);}
    return ::WaitForSingleObject(job->done,4500)==WAIT_OBJECT_0 && job->handled.load()?TRUE:FALSE;
    }catch(...){return FALSE;}
}
}
void install_wa2_kernel64(Process& p){
    auto resources=std::make_shared<ResourceState>();
    api(p,"kernel32.dll","FindResourceA",3,[resources](Process& q,Args a)->U32 {
        if(a[0] && a[0]!=q.image_base){q.set_error(ERROR_INVALID_HANDLE);return 0;}
        const auto item=find_resource(q.source_image(),resource_name(q,a[2]),resource_name(q,a[1]));
        if(!item){q.set_error(ERROR_RESOURCE_NAME_NOT_FOUND);return 0;}
        for(const auto& [id,r]:resources->items)if(r.resource.rva==item->rva && r.resource.size==item->size)return id;
        const auto id=resources->next;resources->next+=16;resources->items.emplace(id,ResourceState::Item{*item});return id;
    });
    api(p,"kernel32.dll","LoadResource",2,[resources](Process& q,Args a)->U32 {
        if(a[0] && a[0]!=q.image_base){q.set_error(ERROR_INVALID_HANDLE);return 0;}
        auto it=resources->items.find(a[1]);if(it==resources->items.end()){q.set_error(ERROR_RESOURCE_DATA_NOT_FOUND);return 0;}
        auto& item=it->second;if(!item.data){const auto data=resource_bytes(q.source_image(),item.resource);item.data=q.allocate_bytes(data.size());q.memory.copy_in(item.data,data);q.memory.protect(item.data,(std::max<std::size_t>(1,data.size())+4095)&~std::size_t(4095),Memory::Read);}
        return item.data;
    });
    api(p,"kernel32.dll","LockResource",1,[resources](Process& q,Args a)->U32 {for(const auto& [id,item]:resources->items)if(item.data && item.data==a[0])return item.data;q.set_error(ERROR_INVALID_HANDLE);return 0;});
    // Windows retains loaded resources for the module lifetime; this obsolete
    // function returns FALSE and does not release the resource bytes.
    api(p,"kernel32.dll","FreeResource",1,[](Process&,Args)->U32 {return FALSE;});
    api(p,"kernel32.dll","EnumSystemLocalesA",2,[](Process& q,Args a)->U32 {
        q.memory.check(a[0],1,Memory::Execute);LocaleCall call{&q,a[0],{}};const auto prior=locale_call;locale_call=&call;
        const auto result=::EnumSystemLocalesA(locale_callback,a[1]);const auto error=::GetLastError();locale_call=prior;
        if(call.failure)std::rethrow_exception(call.failure);if(!result)q.set_error(error);return U32(result);
    });
    for(bool wide:{false,true})api(p,"kernel32.dll",wide?"WriteConsoleW":"WriteConsoleA",5,[wide](Process& q,Args a)->U32 {
        if(a[3]){q.memory.check(a[3],4,Memory::Write);q.memory.store(a[3],0,32);}if(a[4] || a[2]>16*1024*1024){q.set_error(ERROR_INVALID_PARAMETER);return 0;}
        auto it=q.state().files.find(a[0]);if(it==q.state().files.end() || !it->second.stream || !it->second.write){q.set_error(ERROR_INVALID_HANDLE);return 0;}
        const auto handle=reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(it->second.stream)));
        std::vector<std::uint8_t> bytes(std::size_t(a[2])*(wide?2:1));q.memory.copy_out(a[1],bytes);DWORD written=0;
        const auto result=wide?::WriteConsoleW(handle,bytes.data(),a[2],&written,nullptr): ::WriteConsoleA(handle,bytes.data(),a[2],&written,nullptr);
        if(a[3])q.memory.store(a[3],written,32);return boolean(q,result);
    });
    for(bool date:{false,true})api(p,"kernel32.dll",date?"GetDateFormatA":"GetTimeFormatA",6,[date](Process& q,Args a)->U32 {
        if(a[5]>1048576){q.set_error(ERROR_INVALID_PARAMETER);return 0;}std::optional<SYSTEMTIME> time;if(a[2])time=read<SYSTEMTIME>(q,a[2]);const auto format=nullable_string(q,a[3]);
        if(a[5])q.memory.check(a[4],a[5],Memory::Write);std::vector<char> output(a[5]);
        const auto result=date?::GetDateFormatA(a[0],a[1],time?&*time:nullptr,pointer(format),a[5]?output.data():nullptr,int(a[5])): ::GetTimeFormatA(a[0],a[1],time?&*time:nullptr,pointer(format),a[5]?output.data():nullptr,int(a[5]));
        if(result && a[5])q.memory.copy_in(a[4],std::span(reinterpret_cast<const std::uint8_t*>(output.data()),result));if(!result)q.set_error(::GetLastError());return U32(result);
    });
    auto controls=std::make_shared<ControlState>();controls->process=&p;
    api(p,"kernel32.dll","SetConsoleCtrlHandler",2,[controls](Process& q,Args a)->U32 {
        if(!a[0])return boolean(q,::SetConsoleCtrlHandler(nullptr,a[1]!=0));q.memory.check(a[0],1,Memory::Execute);
        if(!controls->registered){std::lock_guard lock(control_mutex);if(auto prior=control_state.lock();prior && prior!=controls){q.set_error(ERROR_BUSY);return 0;}if(!::SetConsoleCtrlHandler(control_callback,TRUE))return boolean(q,FALSE);control_state=controls;controls->registered=true;}
        std::lock_guard lock(controls->mutex);if(a[1])controls->handlers.push_back(a[0]);else{auto it=std::find(controls->handlers.rbegin(),controls->handlers.rend(),a[0]);if(it==controls->handlers.rend()){q.set_error(ERROR_INVALID_PARAMETER);return 0;}controls->handlers.erase(std::next(it).base());}return 1;
    });
    p.state().poll.push_back([controls]{
        std::deque<std::shared_ptr<ControlJob>> jobs;std::vector<U32> handlers;{std::lock_guard lock(controls->mutex);jobs.swap(controls->jobs);handlers=controls->handlers;}
        for(const auto& job:jobs){try{const std::array<U32,1> args{job->code};for(auto it=handlers.rbegin();it!=handlers.rend();++it)if(controls->process->callback(*it,args)){job->handled=true;break;}::SetEvent(job->done);}catch(...){::SetEvent(job->done);throw;}}
    });
    p.state().shutdown.push_back([controls]{std::lock_guard global(control_mutex);std::lock_guard local(controls->mutex);controls->stopped=true;if(controls->registered)::SetConsoleCtrlHandler(control_callback,FALSE);for(auto& job:controls->jobs)::SetEvent(job->done);controls->jobs.clear();if(control_state.lock()==controls)control_state.reset();});
    api(p,"kernel32.dll","FatalAppExitA",2,[](Process& q,Args a)->U32 {const auto text=q.read_string(a[1]);::FatalAppExitA(a[0],text.c_str());return 0;});
}
}
