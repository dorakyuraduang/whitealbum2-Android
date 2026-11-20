#include "extended_system.hpp"
#include "codepage.hpp"
#include "resource_mount.hpp"
#include "../../port/guest_api.hpp"
#include <winrecomp/path_name.hpp>
#include <algorithm>

namespace wa2::platform::compat {namespace {
using namespace wr;using namespace wr::port;
void bind(Process& p,const char* name,unsigned count,std::function<U32(Process&,Args)> function){
    auto wrapper=[count,function=std::move(function)](Process& q){
        std::array<U32,8> values{};for(unsigned n=0;n<count;++n)values[n]=q.argument(n);
        return function(q,std::span(values).first(count));
    };
    if(p.resolve("kernel32.dll",name))p.replace_api("kernel32.dll",name,count,std::move(wrapper));
    else p.register_api("kernel32.dll",name,count,std::move(wrapper));
}
struct Narrow {
    Process& process;U32 address;
    Narrow(Process& p,U32 wide):process(p),address(0){
        const auto value=encode_text(936,p.read_wstring(wide),{},true);
        std::string text(value.bytes.begin(),value.bytes.end());address=p.put_string(text);
    }
    ~Narrow(){if(address)process.memory.release(address);}
};
U32 forward(Process& p,const char* name,Args args){
    const auto target=p.resolve("kernel32.dll",name);
    if(!target)throw GuestFault(FaultKind::unsupported,p.cpu.eip,std::string("missing shared service: ")+name);
    return p.callback(target,args);
}
U32 output(Process& p,U32 address,U32 count,std::u16string_view text,bool module){
    if(!count){if(module)p.set_error(122);return module?0:U32(text.size()+1);}
    if(!module && count<=text.size())return U32(text.size()+1);
    const auto size=U32(std::min<std::size_t>(text.size(),count-1));
    p.memory.check(address,(size+1)*2,Memory::Write);
    for(U32 n=0;n<size;++n)p.memory.store(address+n*2,text[n],16);
    p.memory.store(address+size*2,0,16);
    if(module && size<text.size()){p.set_error(122);return count;}
    return size;
}
U32 directory(Process& p,U32 input){
    try{
        const auto path=guest_file_path(p,p.read_string(input));
        const auto relative=path.lexically_relative(p.options.data_root);
        if(relative.empty() || *relative.begin()=="..")throw GuestFault(FaultKind::unsupported,p.cpu.eip,"current directory escapes selected root");
        const auto name=relative=="."?std::string{}:utf8_file_name(relative);
        bool exists=name.empty();
        if(!exists){
            if(const auto mounted=mounted_resource_info(p,path))exists=bool(mounted->attributes&16);
            else if(auto* files=mutable_files_for(p,path)){const auto info=files->info(name);exists=info && (info->attributes&16);}
            else{std::error_code error;exists=std::filesystem::is_directory(path,error);}
        }
        if(!exists){p.set_error(3);return 0;}
        // guest_file_path already interprets this UTF-8 relative directory.
        p.state().current_directory=name;return 1;
    }catch(const EncodingError&){p.set_error(1113);return 0;}
}
}
void install_extended_system(wr::Process& p){
    bind(p,"LoadLibraryW",1,[](Process& q,Args a){try{Narrow name(q,a[0]);const std::array<U32,1> args{name.address};return forward(q,"LoadLibraryA",args);}catch(const EncodingError&){q.set_error(1113);return 0u;}});
    bind(p,"CreateFileW",7,[](Process& q,Args a){try{Narrow name(q,a[0]);std::array<U32,7> args;std::copy(a.begin(),a.end(),args.begin());args[0]=name.address;return forward(q,"CreateFileA",args);}catch(const EncodingError&){q.set_error(1113);return 0xffffffffu;}});
    bind(p,"GetModuleFileNameW",3,[](Process& q,Args a){
        if(a[0] && a[0]!=q.image_base){q.set_error(6);return 0u;}
        const auto text="C:\\WinRecomp\\"+q.options.image_name;
        return output(q,a[1],a[2],decode_text(936,{reinterpret_cast<const std::uint8_t*>(text.data()),text.size()},true),true);
    });
    bind(p,"GetCurrentDirectoryW",2,[](Process& q,Args a){
        auto text=std::string("C:\\WinRecomp");if(!q.state().current_directory.empty())text+='\\'+q.state().current_directory;
        std::replace(text.begin(),text.end(),'/','\\');
        return output(q,a[1],a[0],decode_text(65001,{reinterpret_cast<const std::uint8_t*>(text.data()),text.size()},true),false);
    });
    bind(p,"SetCurrentDirectoryA",1,[](Process& q,Args a){return directory(q,a[0]);});
    bind(p,"SetCurrentDirectoryW",1,[](Process& q,Args a){try{Narrow name(q,a[0]);return directory(q,name.address);}catch(const EncodingError&){q.set_error(1113);return 0u;}});
    bind(p,"GetStartupInfoW",1,[](Process& q,Args a){
        std::array<std::uint8_t,68> bytes{};q.memory.copy_in(a[0],bytes);q.memory.store(a[0],68,32);
        q.memory.store(a[0]+56,q.state().stdin_handle,32);q.memory.store(a[0]+60,q.state().stdout_handle,32);q.memory.store(a[0]+64,q.state().stderr_handle,32);return 0u;
    });
    bind(p,"FindFirstFileExA",6,[](Process& q,Args a){
        if(a[1]>1 || a[3] || a[4] || (a[5]&~2u)){q.set_error(87);return 0xffffffffu;}
        const auto handle=forward(q,"FindFirstFileA",std::array<U32,2>{a[0],a[2]});
        if(handle!=0xffffffffu && a[1]==1)q.memory.store(a[2]+304,0,8);
        return handle;
    });
    bind(p,"GetDriveTypeW",1,[](Process& q,Args a){
        if(!a[0])return 3u;
        auto name=q.read_wstring(a[0]);for(auto& c:name)if(c>=u'A' && c<=u'Z')c+=u'a'-u'A';
        if(name==u"c:\\" || name==u"c:/")return 3u;return 1u;
    });
    bind(p,"HeapSetInformation",4,[](Process& q,Args a){
        if(a[0] && !q.state().heaps.contains(a[0])){q.set_error(6);return 0u;}
        // Corrupt heap operations already fail explicitly. LFH configuration
        // has no portable allocator contract and returns API failure.
        if(a[1]==1 && !a[2] && !a[3])return 1u;
        q.set_error(a[1]==0?120:87);return 0u;
    });
    bind(p,"IsProcessorFeaturePresent",1,[](Process&,Args a)->U32{
        // Match guest_cpuid's fixed CX8/TSC/SSE/SSE2 contract, never the host CPU.
        return a[0]==2 || a[0]==6 || a[0]==8 || a[0]==10;
    });
}
void install_d3dx43_aliases(wr::Process& p){
    for(const auto& [name,count]:std::array<std::pair<const char*,unsigned>,4>{{
        {"D3DXCreateTexture",8},{"D3DXLoadSurfaceFromMemory",10},
        {"D3DXLoadSurfaceFromSurface",8},{"D3DXAssembleShader",7}}}){
        const auto target=p.resolve("d3dx9_41.dll",name);
        if(!target)throw std::logic_error("missing common D3DX renderer service");
        api(p,"d3dx9_43.dll",name,count,[target](Process& q,Args a){return q.callback(target,a);});
    }
}
}
