#include "legacy_display.hpp"
#include "../window/host.hpp"
#include "../../port/guest_api.hpp"
#include "../../generated/graphics-abi/source_abi.hpp"
#include <winrecomp/directdraw.hpp>
#include <map>
#include <sstream>
namespace wa2::platform::compat {namespace {
using namespace wr;using namespace wr::port;using graphics::abi::GUID;
constexpr GUID draw1{0x6c14db80,0xa733,0x11ce,{0xa5,0x21,0x00,0x20,0xaf,0x0b,0xe5,0x60}};
constexpr GUID draw7{0x15e65ec0,0x3b9c,0x11d2,{0xb9,0x2f,0,0x60,0x97,0x97,0xea,0x5b}};
class Display final:public DirectDrawBackend {
    Process& p;std::shared_ptr<window::Host> host;struct Object {U32 refs,version;};std::map<U32,Object> objects;std::map<U32,U32> tables;unsigned enumerations{};
    [[noreturn]]void unsupported(const std::string& text){throw GuestFault(FaultKind::unsupported,p.cpu.eip,"shared display: "+text);}
    U32 wrap(U32 version){const auto at=p.allocate_bytes(4);p.memory.store(at,vtable(version),32);p.memory.protect(at,4096,Memory::Read);objects.emplace(at,Object{1,version});return at;}
    U32 vtable(U32 version){if(tables.contains(version))return tables.at(version);const unsigned count=version==7?30:23;const auto at=p.allocate_bytes(count*4);
        for(unsigned n=0;n<count;++n){const auto name="Display."+std::to_string(version)+"."+std::to_string(n);unsigned argc=n==0 || n==11 || n==20?3:n==12?2:1;
            api(p,"wa2.display",name,argc,[this,version,n](Process&,Args a){auto found=objects.find(a[0]);if(found==objects.end() || found->second.version!=version)unsupported("invalid COM receiver");auto& o=found->second;
                if(n==0){const auto iid=read<GUID>(p,a[1]);p.memory.check(a[2],4,Memory::Write);p.memory.store(a[2],0,32);if(iid==graphics::abi::IID_IUnknown){++o.refs;p.memory.store(a[2],a[0],32);return U32(0);}if(iid==draw1 || iid==draw7){p.memory.store(a[2],wrap(iid==draw7?7:1),32);return U32(0);}return U32(0x80004002);}
                if(n==1)return ++o.refs;if(n==2){const auto refs=--o.refs;if(!refs){p.memory.release(a[0]);objects.erase(found);}return refs;}
                if(n==11){for(unsigned j=1;j<3;++j)if(a[j]){const auto size=p.memory.load(a[j],32);if(size!=316 && size!=364 && size!=380)unsupported("unknown DDCAPS size");p.memory.check(a[j],size,Memory::Write);std::vector<std::uint8_t> caps(size);p.memory.copy_in(a[j],caps);p.memory.store(a[j],size,32);
                        // This API exposes the shared renderer's bounded native
                        // resource budget, rather than a Windows driver handle.
                        p.memory.store(a[j]+4,j==1?1u:0x02000000u,32);p.memory.store(a[j]+60,128u*1024*1024,32);p.memory.store(a[j]+64,128u*1024*1024,32);}return U32(0);}
                if(n==20){if(a[2]!=8)unsupported("exclusive DirectDraw cooperative mode");if(a[1])host->client(a[1]);return U32(0);}
                if(n==26 && version==7)return U32(0);
                unsupported("COM method "+std::to_string(version)+":"+std::to_string(n));
            });p.memory.store(at+n*4,p.resolve("wa2.display",name),32);}
        p.memory.protect(at,4096,Memory::Read);tables.emplace(version,at);return at;
    }
public:
    Display(Process& process,std::shared_ptr<window::Host> h):p(process),host(std::move(h)){
        api(p,"ddraw.dll","DirectDrawCreate",3,[this](Process&,Args a){p.memory.check(a[1],4,Memory::Write);if(a[2])return U32(0x80040110);if(a[0]>2)unsupported("non-default DirectDraw adapter");p.memory.store(a[1],wrap(1),32);return U32(0);});
        api(p,"ddraw.dll","DirectDrawCreateEx",4,[this](Process&,Args a){p.memory.check(a[1],4,Memory::Write);if(a[3])return U32(0x80040110);if(read<GUID>(p,a[2])!=draw7)return U32(0x80004002);if(a[0]>2)unsupported("non-default DirectDraw adapter");p.memory.store(a[1],wrap(7),32);return U32(0);});
        for(const bool extended:{false,true})api(p,"ddraw.dll",extended?"DirectDrawEnumerateExA":"DirectDrawEnumerateA",extended?3:2,[this,extended](Process&,Args a){if(extended && a[2]&~7u)unsupported("adapter enumeration flags");const auto scratch=p.allocate_bytes(4096);
            try{string(p,scratch,"Shared SDL graphics adapter");string(p,scratch+256,"display");const std::array<U32,5> args{0,scratch,scratch+256,a[1],0};p.callback(a[0],std::span(args).first(extended?5:4));p.memory.release(scratch);++enumerations;return U32(0);}catch(...){p.memory.release(scratch);throw;}});
    }
    void shutdown()noexcept override{objects.clear();}
    std::string report()const override{return "{\"backend\":\"shared-renderer-capabilities\",\"live_objects\":"+std::to_string(objects.size())+",\"enumerations\":"+std::to_string(enumerations)+"}";}
};
}
std::unique_ptr<wr::DirectDrawBackend> install_legacy_display(wr::Process& p,std::shared_ptr<window::Host> host){return std::make_unique<Display>(p,std::move(host));}
}
