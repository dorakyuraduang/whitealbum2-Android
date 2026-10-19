#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <bit>
#include <io.h>
#include "support.hpp"
#include "wa2_win32.hpp"
namespace wr {
using namespace port;
void install_wa2_kernel(Process& p) {
    api(p,"user32.dll","MessageBoxA",4,[](Process& q,Args a)->U32 {
        const auto text=a[1]?q.read_string(a[1]):std::string(),caption=a[2]?q.read_string(a[2]):std::string();
        if(q.state().stop_after_presents){
            const int count=::MultiByteToWideChar(CP_ACP,0,text.data(),int(text.size()),nullptr,0);std::wstring wide(count,L'\0');::MultiByteToWideChar(CP_ACP,0,text.data(),int(text.size()),wide.data(),count);
            const int bytes=::WideCharToMultiByte(CP_UTF8,0,wide.data(),count,nullptr,0,nullptr,nullptr);std::string utf8(bytes,'\0');::WideCharToMultiByte(CP_UTF8,0,wide.data(),count,utf8.data(),bytes,nullptr,nullptr);
            throw GuestFault(FaultKind::unsupported,q.cpu.eip,"Guest MessageBoxA (diagnostic stopped for interaction): "+utf8);
        }
        const auto owner=a[0]?reinterpret_cast<HWND>(q.gui()->native_window(a[0])):nullptr;return U32(::MessageBoxA(owner,text.c_str(),caption.c_str(),a[3]));
    });
    auto kernel=[&](const char* name,unsigned count,std::function<U32(Process&,Args)> fn){api(p,"kernel32.dll",name,count,std::move(fn));};
    kernel("lstrlenA",1,[](Process& q,Args a)->U32 {return a[0]?U32(q.read_string(a[0]).size()):0;});
    kernel("lstrcpyA",2,[](Process& q,Args a){auto value=q.read_string(a[1]);string(q,a[0],value);return a[0];});
    kernel("lstrcmpiA",2,[](Process& q,Args a)->U32 {const auto left=q.read_string(a[0]),right=q.read_string(a[1]);return U32(::lstrcmpiA(left.c_str(),right.c_str()));});
    kernel("Sleep",1,[](Process& q,Args a)->U32 {if(q.state().sleep_thread){q.state().sleep_thread(a[0]);return 0;}if(a[0]>60000)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"Sleep exceeds bounded host wait");::Sleep(a[0]);return 0;});
    kernel("GetConsoleCP",0,[](Process&,Args)->U32 {return ::GetConsoleCP();});
    kernel("GetConsoleOutputCP",0,[](Process&,Args)->U32 {return ::GetConsoleOutputCP();});
    kernel("GetConsoleMode",2,[](Process& q,Args a)->U32 {
        auto it=q.state().files.find(a[0]);if(it==q.state().files.end() || it->second.type!=FILE_TYPE_CHAR){q.set_error(ERROR_INVALID_HANDLE);return 0;}
        if(!it->second.stream){q.set_error(ERROR_INVALID_HANDLE);return 0;}q.memory.check(a[1],4,Memory::Write);
        const auto handle=reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(it->second.stream)));DWORD mode{};
        const auto result=::GetConsoleMode(handle,&mode);if(result)q.memory.store(a[1],mode,32);return boolean(q,result);
    });
    kernel("GetUserDefaultLCID",0,[](Process&,Args)->U32 {return ::GetUserDefaultLCID();});
    for(bool wide:{false,true})kernel(wide?"GetLocaleInfoW":"GetLocaleInfoA",4,[wide](Process& q,Args a)->U32 {
        if(a[3]>65536){q.set_error(ERROR_INVALID_PARAMETER);return 0;}
        if(!a[3]){auto result=wide ? ::GetLocaleInfoW(a[0],a[1],nullptr,0) : ::GetLocaleInfoA(a[0],a[1],nullptr,0);return boolean(q,result);}
        const auto bytes=a[3]*(wide?2u:1u);q.memory.check(a[2],bytes,Memory::Write);
        std::vector<std::uint16_t> buffer((bytes+1)/2);
        const auto result=wide ? ::GetLocaleInfoW(a[0],a[1],reinterpret_cast<wchar_t*>(buffer.data()),int(a[3])) : ::GetLocaleInfoA(a[0],a[1],reinterpret_cast<char*>(buffer.data()),int(a[3]));
        if(result)q.memory.copy_in(a[2],std::span(reinterpret_cast<const std::uint8_t*>(buffer.data()),std::size_t(result)*(wide?2:1)));
        return boolean(q,result);
    });
    kernel("FreeLibrary",1,[](Process& q,Args a)->U32 {if(q.module_name(a[0]).empty()){q.set_error(ERROR_INVALID_HANDLE);return 0;}return 1;});
    kernel("IsDebuggerPresent",0,[](Process&,Args)->U32 {return 0;});
    kernel("GetSystemInfo",1,[](Process& q,Args a)->U32 {
        SYSTEM_INFO info{};::GetSystemInfo(&info);
        const std::array<U32,9> legacy{0,info.dwPageSize,0x10000,0x7ffeffff,U32(info.dwActiveProcessorMask),info.dwNumberOfProcessors,PROCESSOR_INTEL_PENTIUM,info.dwAllocationGranularity,U32(info.wProcessorLevel)|(U32(info.wProcessorRevision)<<16)};
        write(q,a[0],legacy);return 0;
    });
    kernel("GetFullPathNameA",4,[](Process& q,Args a)->U32 {
        const auto native=guest_file_path(q,q.read_string(a[0]));const auto local=native.lexically_relative(q.options.data_root);
        std::string full;if(local.empty() || (local.begin()!=local.end() && *local.begin()==".."))full=native.string();else{full="C:\\WinRecomp";if(local!="."){full+='\\';full+=local.string();}}std::replace(full.begin(),full.end(),'/','\\');
        if(a[3])q.memory.check(a[3],4,Memory::Write);
        if(a[1]<=full.size())return U32(full.size()+1);
        string(q,a[2],full);if(a[3])q.memory.store(a[3],a[2]+U32(full.find_last_of('\\')+1),32);return U32(full.size());
    });
    kernel("SetCurrentDirectoryA",1,[](Process& q,Args a)->U32 {
        const auto path=guest_file_path(q,q.read_string(a[0]));std::error_code ec;
        if(!std::filesystem::is_directory(path,ec)){q.set_error(ERROR_PATH_NOT_FOUND);return 0;}
        const auto relative=path.lexically_relative(q.options.data_root);q.state().current_directory=relative.empty() || (relative.begin()!=relative.end() && *relative.begin()=="..")?path.generic_string():relative=="."?"":relative.generic_string();return 1;
    });
    kernel("CopyFileA",3,[](Process& q,Args a)->U32 {
        const auto from=guest_file_path(q,q.read_string(a[0])),to=guest_file_path(q,q.read_string(a[1]));
        if(!q.options.allow_file_write){q.set_error(ERROR_ACCESS_DENIED);return 0;}return boolean(q,::CopyFileW(from.c_str(),to.c_str(),a[2]!=0));
    });
    kernel("GetPrivateProfileStringA",6,[](Process& q,Args a)->U32 {
        const auto section=nullable_string(q,a[0]),key=nullable_string(q,a[1]),fallback=nullable_string(q,a[2]);
        const auto path=guest_file_path(q,q.read_string(a[5]));
        if(!a[4])return 0;if(a[4]>1048576)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"INI output exceeds staging limit");
        q.memory.check(a[3],a[4],Memory::Write);std::vector<char> output(a[4]);
        const auto result=::GetPrivateProfileStringA(pointer(section),pointer(key),pointer(fallback),output.data(),a[4],path.string().c_str());
        q.memory.copy_in(a[3],std::span(reinterpret_cast<const std::uint8_t*>(output.data()),output.size()));return result;
    });
    kernel("GetPrivateProfileIntA",4,[](Process& q,Args a)->U32 {
        const auto section=nullable_string(q,a[0]),key=nullable_string(q,a[1]);const auto path=guest_file_path(q,q.read_string(a[3]));
        return ::GetPrivateProfileIntA(pointer(section),pointer(key),int(a[2]),path.string().c_str());
    });
    kernel("WritePrivateProfileStringA",4,[](Process& q,Args a)->U32 {
        if(!q.options.allow_file_write){q.set_error(ERROR_ACCESS_DENIED);return 0;}
        const auto section=nullable_string(q,a[0]),key=nullable_string(q,a[1]),value=nullable_string(q,a[2]);const auto path=guest_file_path(q,q.read_string(a[3]));
        return boolean(q,::WritePrivateProfileStringA(pointer(section),pointer(key),pointer(value),path.string().c_str()));
    });
    for(bool writing:{false,true})kernel(writing?"WritePrivateProfileStructA":"GetPrivateProfileStructA",5,[writing](Process& q,Args a)->U32 {
        if(writing && !q.options.allow_file_write){q.set_error(ERROR_ACCESS_DENIED);return 0;}
        if(a[3]>1048576)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"INI structure exceeds staging limit");
        const auto section=nullable_string(q,a[0]),key=nullable_string(q,a[1]);const auto path=guest_file_path(q,q.read_string(a[4]));
        std::vector<std::uint8_t> bytes(a[3]);if(a[2]){q.memory.check(a[2],a[3],writing?Memory::Read:Memory::Write);if(writing)q.memory.copy_out(a[2],bytes);}
        const auto result=writing ? ::WritePrivateProfileStructA(pointer(section),pointer(key),a[2]?bytes.data():nullptr,a[3],path.string().c_str()) : ::GetPrivateProfileStructA(pointer(section),pointer(key),a[2]?bytes.data():nullptr,a[3],path.string().c_str());
        if(result && !writing && a[2])q.memory.copy_in(a[2],bytes);return boolean(q,result);
    });
}
}
