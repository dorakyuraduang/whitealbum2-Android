// Shell COM objects live in guest memory. Their vtables contain dispatch tokens,
// never native pointers. Special folders use the runtime's virtual C: drive.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shellapi.h>
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include "wa2_win32.hpp"
#include "selection.hpp"

namespace wr {
namespace {
using Args=std::span<const U32>;
using port::read;using port::string;using port::selected_path;
struct Shell {
    U32 allocator{},references{};
    unsigned com_initializations{};
    std::map<U32,PIDLIST_ABSOLUTE> native_pidls;
    std::map<U32,HINSTANCE> executions;U32 next_execution{0xbb000000};
    ~Shell(){for(const auto& [id,pidl]:native_pidls)::CoTaskMemFree(pidl);while(com_initializations){::CoUninitialize();--com_initializations;}}
    std::map<U32,U32> blocks;
    std::map<U32,std::string> pidls;
    U32 allocate(Process& p,U32 size) {
        if(size>128u*1024*1024)return 0;
        const auto at=p.virtual_alloc(0,std::max(size,1u),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if(at){p.state().virtual_reservations.erase(at);blocks.emplace(at,size);}return at;
    }
    void free(Process& p,U32 at) {
        if(!at)return;
        if(!blocks.contains(at))throw GuestFault(FaultKind::memory,at,"foreign Shell allocator pointer");
        p.memory.release(at);blocks.erase(at);pidls.erase(at);if(auto it=native_pidls.find(at);it!=native_pidls.end()){::CoTaskMemFree(it->second);native_pidls.erase(it);}
    }
    U32 reallocate(Process& p,U32 at,U32 size) {
        if(!at)return allocate(p,size);
        auto it=blocks.find(at);
        if(it==blocks.end())throw GuestFault(FaultKind::memory,at,"foreign Shell realloc pointer");
        if(!size){free(p,at);return 0;}
        if(size>128u*1024*1024)return 0;
        if(size<=p.memory.allocation(at).size){it->second=size;pidls.erase(at);if(auto native=native_pidls.find(at);native!=native_pidls.end()){::CoTaskMemFree(native->second);native_pidls.erase(native);}return at;}
        const auto replacement=allocate(p,size);if(!replacement)return 0;
        try{std::vector<std::uint8_t> bytes(std::min(it->second,size));p.memory.copy_out(at,bytes);p.memory.copy_in(replacement,bytes);}
        catch(...){free(p,replacement);throw;}
        free(p,at);return replacement;
    }
    void check_this(Process& p,U32 at) {
        if(at!=allocator || !references)throw GuestFault(FaultKind::memory,at,"invalid Shell IMalloc object");
        p.memory.check(at,4,Memory::Read);
    }
};
struct BrowseCall {Process* process;std::shared_ptr<Shell> state;U32 callback{},data{};std::exception_ptr failure;};
thread_local BrowseCall* current_browse{};
U32 store_native_pidl(Process& p,Shell& state,PIDLIST_ABSOLUTE native){
    const auto at=state.allocate(p,8);if(!at){::CoTaskMemFree(native);return 0;}p.memory.store(at,4,16);p.memory.store(at+2,0xa3,16);p.memory.store(at+4,0,16);state.native_pidls.emplace(at,native);return at;
}
int CALLBACK browse_callback(HWND hwnd,UINT message,LPARAM parameter,LPARAM) noexcept {
    auto& call=*current_browse;U32 temporary=0;
    try{
        auto& p=*call.process;const auto window=p.gui()->native_window_token(reinterpret_cast<std::uintptr_t>(hwnd));U32 value=U32(parameter);
        if(message==BFFM_SELCHANGED){auto clone=::ILCloneFull(reinterpret_cast<PCIDLIST_ABSOLUTE>(parameter));if(!clone)throw std::bad_alloc();temporary=store_native_pidl(p,*call.state,clone);value=temporary;}
        else if(message==BFFM_VALIDATEFAILEDA){temporary=p.put_string(reinterpret_cast<const char*>(parameter));value=temporary;}
        const std::array<U32,4> args{window,message,value,call.data};const auto result=p.callback(call.callback,args);
        if(temporary){if(message==BFFM_SELCHANGED)call.state->free(p,temporary);else p.memory.release(temporary);}return std::bit_cast<std::int32_t>(result);
    }catch(...){if(temporary)try{if(message==BFFM_SELCHANGED)call.state->free(*call.process,temporary);else call.process->memory.release(temporary);}catch(...){}call.failure=std::current_exception();::PostMessageA(hwnd,WM_CLOSE,0,0);return 0;}
}
void api(Process& p,const char* dll,const char* name,unsigned count,std::function<U32(Process&,Args)> fn) {
    p.register_api(dll,name,count,[count,fn=std::move(fn)](Process& q){
        std::array<U32,8> args{};for(unsigned n=0;n<count;++n)args[n]=q.argument(n);
        return fn(q,std::span(args).first(count));
    });
}
std::optional<std::string> folder(U32 csidl) {
    switch(csidl&0xffu) {
    case 0x00:case 0x10:return "C:\\WinRecomp\\Desktop";
    case 0x05:return "C:\\WinRecomp\\Documents";
    case 0x1a:return "C:\\WinRecomp\\AppData\\Roaming";
    case 0x1c:return "C:\\WinRecomp\\AppData\\Local";
    case 0x23:return "C:\\WinRecomp\\ProgramData";
    case 0x28:return "C:\\WinRecomp";
    default:return {};
    }
}
}
void install_wa2_shell(Process& p) {
    const auto state=std::make_shared<Shell>();
    p.state().native_pidl=[state,&p](U32 id)->std::uintptr_t{if(auto it=state->native_pidls.find(id);it!=state->native_pidls.end())return reinterpret_cast<std::uintptr_t>(it->second);auto folder=state->pidls.find(id);if(folder==state->pidls.end())throw GuestFault(FaultKind::memory,id,"unknown guest PIDL");const auto path=guest_file_path(p,folder->second);PIDLIST_ABSOLUTE native=nullptr;const auto hr=::SHParseDisplayName(path.c_str(),nullptr,&native,0,nullptr);if(FAILED(hr)){p.set_error(HRESULT_CODE(hr));return 0;}state->native_pidls.emplace(id,native);return reinterpret_cast<std::uintptr_t>(native);};
    auto method=[&](unsigned slot,const char* name,unsigned count,std::function<U32(Process&,Args)> fn) {
        const auto key=std::string("IMalloc.")+name;
        p.register_api("wa2.shell",key,count,[state,count,fn=std::move(fn)](Process& q){
            state->check_this(q,q.argument(0));std::array<U32,4> args{};
            for(unsigned n=0;n<count;++n)args[n]=q.argument(n);
            return fn(q,std::span(args).first(count));
        });
        return p.resolve("wa2.shell",key);
    };
    std::array<U32,9> table{};
    table[0]=method(0,"QueryInterface",3,[state](Process& q,Args a)->U32 {
        if(!a[2])return U32(E_POINTER);q.memory.check(a[2],4,Memory::Write);q.memory.store(a[2],0,32);
        std::array<std::uint8_t,16> iid{};q.memory.copy_out(a[1],iid);
        constexpr std::array<std::uint8_t,16> unknown{0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
        constexpr std::array<std::uint8_t,16> malloc_id{2,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
        if(iid!=unknown && iid!=malloc_id)return U32(E_NOINTERFACE);
        ++state->references;q.memory.store(a[2],state->allocator,32);return S_OK;
    });
    table[1]=method(1,"AddRef",1,[state](Process&,Args){return ++state->references;});
    table[2]=method(2,"Release",1,[state](Process&,Args){return --state->references;});
    table[3]=method(3,"Alloc",2,[state](Process& q,Args a){return state->allocate(q,a[1]);});
    table[4]=method(4,"Realloc",3,[state](Process& q,Args a){return state->reallocate(q,a[1],a[2]);});
    table[5]=method(5,"Free",2,[state](Process& q,Args a)->U32 {state->free(q,a[1]);return 0;});
    table[6]=method(6,"GetSize",2,[state](Process& q,Args a)->U32 {
        if(!a[1])return 0xffffffffu;
        if(!state->blocks.contains(a[1]))throw GuestFault(FaultKind::memory,a[1],"foreign Shell GetSize pointer");
        return U32(q.memory.allocation(a[1]).size);
    });
    table[7]=method(7,"DidAlloc",2,[state](Process&,Args a)->U32 {return a[1]?U32(state->blocks.contains(a[1])):0xffffffffu;});
    table[8]=method(8,"HeapMinimize",1,[](Process&,Args)->U32 {return 0;}); // Free releases complete guest reservations immediately.
    api(p,"shell32.dll","SHGetMalloc",1,[state,table](Process& q,Args a)->U32 {
        if(!a[0])return U32(E_POINTER);q.memory.check(a[0],4,Memory::Write);
        if(!state->allocator) {
            const auto vtable=q.allocate_bytes(table.size()*4);
            for(unsigned n=0;n<table.size();++n)q.memory.store(vtable+n*4,table[n],32);
            q.memory.protect(vtable,4096,Memory::Read);
            state->allocator=q.allocate_bytes(4);q.memory.store(state->allocator,vtable,32);
            q.memory.protect(state->allocator,4096,Memory::Read);
        }
        ++state->references;q.memory.store(a[0],state->allocator,32);return S_OK;
    });
    api(p,"shell32.dll","SHGetSpecialFolderLocation",3,[state](Process& q,Args a)->U32 {
        if(!a[2])return U32(E_POINTER);q.memory.check(a[2],4,Memory::Write);q.memory.store(a[2],0,32);
        const auto path=folder(a[1]);if(!path)return U32(E_INVALIDARG);
        if(q.options.allow_file_write){std::error_code ec;std::filesystem::create_directories(guest_file_path(q,*path),ec);if(ec)return U32(HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND));}
        // A private, pointer-free filesystem PIDL, consumed by the path bridge.
        const auto cb=U32(path->size()+4);const auto at=state->allocate(q,cb+2);if(!at)return U32(E_OUTOFMEMORY);
        q.memory.store(at,cb,16);q.memory.store(at+2,0xa2,8);
        q.memory.copy_in(at+3,std::span(reinterpret_cast<const std::uint8_t*>(path->c_str()),path->size()+1));
        q.memory.store(at+cb,0,16);state->pidls.emplace(at,*path);q.memory.store(a[2],at,32);return S_OK;
    });
    api(p,"shell32.dll","SHGetPathFromIDListA",2,[state](Process& q,Args a)->U32 {
        if(!a[1])return 0;if(auto native=state->native_pidls.find(a[0]);native!=state->native_pidls.end()){std::array<char,MAX_PATH> path{};if(!::SHGetPathFromIDListA(native->second,path.data()))return 0;string(q,a[1],path.data());return 1;}auto it=state->pidls.find(a[0]);if(it==state->pidls.end())return 0;
        const auto size=it->second.size()+1;q.memory.check(a[0],size+5,Memory::Read);q.memory.check(a[1],size,Memory::Write);
        q.memory.copy_in(a[1],std::span(reinterpret_cast<const std::uint8_t*>(it->second.c_str()),size));return 1;
    });
    api(p,"ole32.dll","CoTaskMemAlloc",1,[state](Process& q,Args a){return state->allocate(q,a[0]);});
    api(p,"ole32.dll","CoTaskMemRealloc",2,[state](Process& q,Args a){return state->reallocate(q,a[0],a[1]);});
    api(p,"ole32.dll","CoTaskMemFree",1,[state](Process& q,Args a)->U32 {state->free(q,a[0]);return 0;});
    api(p,"ole32.dll","CoInitialize",1,[state](Process&,Args a)->U32 {
        if(a[0])return U32(E_INVALIDARG);const auto result=::CoInitialize(nullptr);if(SUCCEEDED(result))++state->com_initializations;return U32(result);
    });
    api(p,"ole32.dll","CoUninitialize",0,[state](Process&,Args)->U32 {if(state->com_initializations){::CoUninitialize();--state->com_initializations;}return 0;});
    api(p,"shell32.dll","ShellExecuteA",6,[state](Process& q,Args a)->U32 {
        const auto operation=a[1]?std::optional(q.read_string(a[1])):std::nullopt;
        auto file=a[2]?q.read_string(a[2]):std::string();if(!file.empty() && file.find("://")==std::string::npos)file=guest_file_path(q,file).string();
        const auto parameters=a[3]?std::optional(q.read_string(a[3])):std::nullopt;
        const auto directory=a[4]?std::optional(guest_file_path(q,q.read_string(a[4])).string()):std::nullopt;
        const auto result=::ShellExecuteA(reinterpret_cast<HWND>(q.gui()->native_window(a[0])),operation?operation->c_str():nullptr,a[2]?file.c_str():nullptr,parameters?parameters->c_str():nullptr,directory?directory->c_str():nullptr,std::bit_cast<std::int32_t>(a[5]));
        const auto code=reinterpret_cast<std::uintptr_t>(result);if(code<=32)return U32(code);const auto id=state->next_execution;state->next_execution+=16;state->executions.emplace(id,result);return id;
    });
    api(p,"shell32.dll","SHBrowseForFolderA",1,[state](Process& q,Args args)->U32 {
        const auto a=read<std::array<U32,8>>(q,args[0]);if(a[2])q.memory.check(a[2],MAX_PATH,Memory::Write);
        BROWSEINFOA native{};native.hwndOwner=reinterpret_cast<HWND>(q.gui()->native_window(a[0]));native.ulFlags=a[4];const auto title=a[3]?q.read_string(a[3]):std::string();native.lpszTitle=a[3]?title.c_str():nullptr;std::array<char,MAX_PATH> display{};native.pszDisplayName=display.data();
        PIDLIST_ABSOLUTE root=nullptr;
        if(a[1]){if(auto it=state->native_pidls.find(a[1]);it!=state->native_pidls.end())native.pidlRoot=it->second;else{auto folder_it=state->pidls.find(a[1]);if(folder_it==state->pidls.end()){q.set_error(ERROR_INVALID_HANDLE);return 0;}const auto path=guest_file_path(q,folder_it->second);if(FAILED(::SHParseDisplayName(path.c_str(),nullptr,&root,0,nullptr)))return 0;native.pidlRoot=root;}}
        BrowseCall call{&q,state,a[5],a[6],{}};if(a[5]){q.memory.check(a[5],1,Memory::Execute);native.lpfn=browse_callback;native.lParam=LONG_PTR(std::bit_cast<std::int32_t>(a[6]));}
        const auto prior=current_browse;current_browse=&call;const auto result=::SHBrowseForFolderA(&native);current_browse=prior;if(root)::CoTaskMemFree(root);if(call.failure){if(result)::CoTaskMemFree(result);std::rethrow_exception(call.failure);}if(!result)return 0;
        if(a[2])q.memory.copy_in(a[2],std::span(reinterpret_cast<const std::uint8_t*>(display.data()),display.size()));q.memory.store(args[0]+28,U32(native.iImage),32);
        std::array<char,MAX_PATH> path{};if(::SHGetPathFromIDListA(result,path.data()))selected_path(q,path.data(),true);return store_native_pidl(q,*state,result);
    });
}
}
