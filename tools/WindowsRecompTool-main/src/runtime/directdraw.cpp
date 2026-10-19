#include "winrecomp/directdraw.hpp"
#include "winrecomp/process.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <exception>
#include <map>
#include <sstream>
#include <thread>
#include <vector>
#include <utility>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
#endif
namespace wr {
#ifndef _WIN32
namespace {class UnavailableDraw final:public DirectDrawBackend {
public:void shutdown() noexcept override{}std::string report() const override{return "{\"backend\":\"unavailable\"}";}
};}
std::unique_ptr<DirectDrawBackend> install_directdraw(Process&){return std::make_unique<UnavailableDraw>();}
#else
namespace {
using Args=std::span<const U32>;
using Function=std::function<U32(Args)>;
enum class Interface {unknown,draw1,draw7,surface7};
constexpr std::size_t DescSize=124,MaxSurfaceBytes=16u*1024*1024;
constexpr U32 MaxObjects=256;
struct Allocation {
    Process& p;U32 address{};
    Allocation(Process& q,std::size_t n):p(q),address(q.allocate_bytes(n)){}
    ~Allocation(){if(address)try{p.memory.release(address);}catch(...){}}
    U32 keep(){return std::exchange(address,0u);}
};
struct NativeOwner {
    IUnknown* value{};
    ~NativeOwner(){if(value)value->Release();}
    IUnknown* keep(){return std::exchange(value,nullptr);}
};
struct SurfaceLock {
    DDSURFACEDESC2 description{};U32 guest{};std::size_t size{},row_bytes{};bool readonly{};
};
struct Object {
    Interface kind{};IUnknown* native{};U32 guest_refs{};
    std::unique_ptr<SurfaceLock> locked;
};
class Draw final:public DirectDrawBackend {
    Process& p;
    DWORD thread{GetCurrentThreadId()};bool stopped{};
    std::map<U32,Object> objects;
    std::map<Interface,U32> vtables;
    std::map<std::uintptr_t,U32> monitors;
    std::uint64_t created{},retired{},enumerated{},locks{},unlocks{},blits{};
    [[noreturn]]void stop(const std::string& why){throw GuestFault(FaultKind::unsupported,p.cpu.eip,"DirectDraw: "+why);}
    void enter(){if(stopped || GetCurrentThreadId()!=thread)stop("foreign-thread or retired backend");}
    GUID guid(U32 address){GUID value{};static_assert(sizeof(value)==16);p.memory.copy_out(address,std::span(reinterpret_cast<std::uint8_t*>(&value),16));return value;}
    Object& object(U32 address,Interface kind){auto it=objects.find(address);if(it==objects.end() || !it->second.native || it->second.kind!=kind)stop("stale or wrong-interface guest COM object");return it->second;}
    void api(const char* name,unsigned count,Function function){
        p.register_api("ddraw.dll",name,count,[this,count,function=std::move(function)](Process& q){enter();std::array<U32,8> args{};for(unsigned n=0;n<count;++n)args[n]=q.argument(n);return function(std::span(args).first(count));});
    }
    U32 wrap(IUnknown* native,Interface kind){
        NativeOwner owner{native};
        for(auto& [address,value]:objects)if(value.native==native && value.kind==kind){if(value.guest_refs==0xffffffffu)stop("reference count overflow");++value.guest_refs;owner.keep();return address;}
        if(objects.size()>=MaxObjects)stop("COM object budget exhausted");
        const auto table=vtable(kind);Allocation memory(p,4);p.memory.store(memory.address,table,32);p.memory.protect(memory.address,4096,Memory::Read);
        const auto at=memory.address;objects.emplace(at,Object{kind,native,1,{}});memory.keep();owner.keep();++created;return at;
    }
    U32 query(Object& value,Args args){
        const auto iid=guid(args[1]);p.memory.check(args[2],4,Memory::Write);
        Interface kind=Interface::unknown;bool supported=true;
        if(IsEqualIID(iid,IID_IUnknown))kind=Interface::unknown;
        else if(IsEqualIID(iid,IID_IDirectDraw))kind=Interface::draw1;
        else if(IsEqualIID(iid,IID_IDirectDraw7))kind=Interface::draw7;
        else if(IsEqualIID(iid,IID_IDirectDrawSurface7))kind=Interface::surface7;
        else supported=false;
        void* result=nullptr;const auto hr=value.native->QueryInterface(iid,&result);
        if(FAILED(hr)){p.memory.store(args[2],0,32);return U32(hr);}
        if(!supported){static_cast<IUnknown*>(result)->Release();stop("native QueryInterface supports an interface without a guest wrapper");}
        p.memory.store(args[2],wrap(static_cast<IUnknown*>(result),kind),32);return U32(hr);
    }
    void destroy_lock(Object& value) noexcept {
        if(!value.locked)return;
        static_cast<IDirectDrawSurface7*>(value.native)->Unlock(nullptr);
        try{p.memory.release(value.locked->guest);}catch(...){}
        value.locked.reset();
    }
    U32 release(Object& value){
        if(value.locked)stop("release of a locked surface");
        const auto refs=value.native->Release();if(!--value.guest_refs){value.native=nullptr;++retired;}return refs;
    }
    U32 vtable(Interface);
    struct Enumeration {Draw* self;U32 callback,context;std::exception_ptr failure;unsigned visits{};bool extended{true};};
    static BOOL CALLBACK enumerate(GUID* id,LPSTR description,LPSTR name,LPVOID context,HMONITOR monitor) noexcept {
        auto& call=*static_cast<Enumeration*>(context);auto& self=*call.self;
        try {
            self.enter();if(call.failure)return FALSE;
            if(++call.visits>256)self.stop("adapter enumeration budget");
            Allocation scratch(self.p,16384);U32 used=0;
            auto bytes=[&](const void* data,std::size_t n){if(n>16384-used)self.stop("adapter callback payload too large");auto at=scratch.address+used;used+=(U32(n)+3)&~3u;self.p.memory.copy_in(at,std::span(static_cast<const std::uint8_t*>(data),n));return at;};
            auto string=[&](const char* text)->U32{if(!text)return 0;std::size_t n=0;while(n<4096 && text[n])++n;if(n==4096)self.stop("unterminated native adapter name");return bytes(text,n+1);};
            U32 mon=0;if(monitor){const auto key=reinterpret_cast<std::uintptr_t>(monitor);auto it=self.monitors.find(key);if(it==self.monitors.end()){if(self.monitors.size()>=256)self.stop("monitor token budget");mon=0xba000000u+U32(self.monitors.size())*16;self.monitors.emplace(key,mon);}else mon=it->second;}
            const std::array<U32,5> args{id?bytes(id,16):0,string(description),string(name),call.context,mon};
            ++self.enumerated;const auto result=self.p.callback(call.callback,std::span(args).first(call.extended?5:4));return self.p.exited()?FALSE:result!=0;
        }catch(...){call.failure=std::current_exception();return FALSE;}
    }
    static BOOL CALLBACK enumerate_legacy(GUID* id,LPSTR description,LPSTR name,LPVOID context) noexcept {return enumerate(id,description,name,context,nullptr);}
    DDSURFACEDESC2 read_description(U32 at){
        p.memory.check(at,DescSize,Memory::Read);if(p.memory.load(at,32)!=DescSize)stop("DDSURFACEDESC2 must have the 124-byte x86 layout");
        DDSURFACEDESC2 d{};d.dwSize=sizeof(d);d.dwFlags=p.memory.load(at+4,32);d.dwHeight=p.memory.load(at+8,32);d.dwWidth=p.memory.load(at+12,32);
        const auto allowed=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT|DDSD_PIXELFORMAT;
        if((d.dwFlags&~allowed) || (d.dwFlags&allowed)!=allowed)stop("only explicit offscreen dimensions/pixel format/caps are supported");
        if(p.memory.load(at+36,32))stop("caller-owned native surface memory is not supported");
        static_assert(sizeof(DDPIXELFORMAT)==32 && sizeof(DDSCAPS2)==16);
        p.memory.copy_out(at+72,std::span(reinterpret_cast<std::uint8_t*>(&d.ddpfPixelFormat),32));
        p.memory.copy_out(at+104,std::span(reinterpret_cast<std::uint8_t*>(&d.ddsCaps),16));
        if(d.ddsCaps.dwCaps!=(DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY) || d.ddsCaps.dwCaps2 || d.ddsCaps.dwCaps3 || d.ddsCaps.dwCaps4)stop("only system-memory offscreen surfaces are supported");
        if(d.ddpfPixelFormat.dwSize!=32 || d.ddpfPixelFormat.dwFlags!=DDPF_RGB || d.ddpfPixelFormat.dwRGBBitCount!=32 || d.ddpfPixelFormat.dwRBitMask!=0xff0000 || d.ddpfPixelFormat.dwGBitMask!=0xff00 || d.ddpfPixelFormat.dwBBitMask!=0xff || d.ddpfPixelFormat.dwRGBAlphaBitMask)stop("only X8R8G8B8 surfaces are supported");
        if(!d.dwWidth || !d.dwHeight || std::uint64_t(d.dwWidth)*d.dwHeight>MaxSurfaceBytes/4)stop("surface allocation budget");return d;
    }
    void write_description(U32 at,const DDSURFACEDESC2& d,U32 pixels=0){
        p.memory.check(at,DescSize,Memory::Write);
        std::array<U32,31> out{};out[0]=DescSize;out[1]=d.dwFlags;out[2]=d.dwHeight;out[3]=d.dwWidth;out[4]=U32(d.lPitch);out[5]=d.dwBackBufferCount;out[6]=d.dwMipMapCount;out[7]=d.dwAlphaBitDepth;out[9]=pixels;
        std::memcpy(out.data()+10,&d.ddckCKDestOverlay,32);std::memcpy(out.data()+18,&d.ddpfPixelFormat,32);std::memcpy(out.data()+26,&d.ddsCaps,16);out[30]=d.dwTextureStage;
        for(unsigned n=0;n<out.size();++n)p.memory.store(at+4*n,out[n],32);
    }
    U32 lock(Object& value,Args a){
        if(a[1] || a[4])stop("surface Lock supports the whole surface and no event handle");
        if(a[3]&~U32(DDLOCK_WAIT|DDLOCK_READONLY|DDLOCK_WRITEONLY|DDLOCK_NOSYSLOCK|DDLOCK_DONOTWAIT))stop("unimplemented Lock flags");
        if(value.locked)stop("surface already locked");
        p.memory.check(a[2],DescSize,Memory::Read|Memory::Write);if(p.memory.load(a[2],32)!=DescSize)stop("Lock description size");
        auto s=static_cast<IDirectDrawSurface7*>(value.native);auto state=std::make_unique<SurfaceLock>();auto& d=state->description;d.dwSize=sizeof(d);
        const auto hr=s->Lock(nullptr,&d,a[3],nullptr);if(FAILED(hr))return U32(hr);
        try {
            const std::int64_t pitch=d.lPitch;const auto stride=std::uint64_t(pitch<0?-pitch:pitch);const auto row=std::uint64_t(d.dwWidth)*4;
            const auto bytes=stride*std::uint64_t(d.dwHeight?d.dwHeight-1:0)+row;
            if(!d.lpSurface || !d.dwHeight || !d.dwWidth || d.ddpfPixelFormat.dwRGBBitCount!=32 || !(d.ddpfPixelFormat.dwFlags&DDPF_RGB) || stride<row || bytes>MaxSurfaceBytes)stop("unhandled native lock shape");
            Allocation staging(p,std::size_t(bytes));state->size=std::size_t(bytes);state->row_bytes=std::size_t(row);state->readonly=(a[3]&DDLOCK_READONLY)!=0;
            const auto top=staging.address+(pitch<0?U32(stride*(d.dwHeight-1)):0);
            for(U32 y=0;y<d.dwHeight;++y){const auto offset=std::int64_t(y)*pitch;auto* native=static_cast<const std::uint8_t*>(d.lpSurface)+offset;p.memory.copy_in(U32(std::int64_t(top)+offset),std::span(native,std::size_t(row)));}
            if(state->readonly)p.memory.protect(staging.address,(bytes+4095)&~4095ull,Memory::Read);
            write_description(a[2],d,top);state->guest=staging.keep();value.locked=std::move(state);++locks;return U32(hr);
        }catch(...){s->Unlock(nullptr);throw;}
    }
    U32 unlock(Object& value,Args a){
        if(a[1])stop("surface Unlock rectangle not supported");
        if(!value.locked)return U32(static_cast<IDirectDrawSurface7*>(value.native)->Unlock(nullptr));
        auto& state=*value.locked;const auto& d=state.description;const std::int64_t pitch=d.lPitch;
        const auto top=state.guest+(pitch<0?U32(-pitch*(d.dwHeight-1)):0);
        if(!state.readonly){
            // Validate and stage everything before touching a locked native surface.
            std::vector<std::uint8_t> bytes(state.row_bytes*d.dwHeight);
            for(U32 y=0;y<d.dwHeight;++y)p.memory.copy_out(U32(std::int64_t(top)+std::int64_t(y)*pitch),std::span(bytes).subspan(y*state.row_bytes,state.row_bytes));
            for(U32 y=0;y<d.dwHeight;++y)std::memcpy(static_cast<std::uint8_t*>(d.lpSurface)+std::int64_t(y)*pitch,bytes.data()+y*state.row_bytes,state.row_bytes);
        }
        auto result=static_cast<IDirectDrawSurface7*>(value.native)->Unlock(nullptr);if(SUCCEEDED(result)){p.memory.release(state.guest);value.locked.reset();++unlocks;}return U32(result);
    }
    void install(){
        api("DirectDrawEnumerateA",2,[this](Args a){p.memory.check(a[0],1,Memory::Execute);Enumeration call{this,a[0],a[1],{},0,false};const auto hr=::DirectDrawEnumerateA(enumerate_legacy,&call);if(call.failure)std::rethrow_exception(call.failure);return U32(hr);});
        api("DirectDrawEnumerateExA",3,[this](Args a){p.memory.check(a[0],1,Memory::Execute);Enumeration call{this,a[0],a[1],{},0};const auto hr=DirectDrawEnumerateExA(enumerate,&call,a[2]);if(call.failure)std::rethrow_exception(call.failure);return U32(hr);});
        api("DirectDrawCreateEx",4,[this](Args a){
            p.memory.check(a[1],4,Memory::Write);const auto iid=guid(a[2]);if(!IsEqualIID(iid,IID_IDirectDraw7))return U32(DDERR_INVALIDPARAMS);if(a[3])stop("COM aggregation is not supported");
            GUID id{};GUID* ptr=nullptr;if(a[0] && a[0]<=2)ptr=reinterpret_cast<GUID*>(std::uintptr_t(a[0]));else if(a[0]){id=guid(a[0]);ptr=&id;}
            IDirectDraw7* object=nullptr;const auto hr=DirectDrawCreateEx(ptr,reinterpret_cast<void**>(&object),IID_IDirectDraw7,nullptr);if(SUCCEEDED(hr))p.memory.store(a[1],wrap(object,Interface::draw7),32);return U32(hr);
        });
        api("DirectDrawCreate",3,[this](Args a){p.memory.check(a[1],4,Memory::Write);if(a[2])stop("COM aggregation is not supported");GUID id{};GUID* ptr=nullptr;if(a[0] && a[0]<=2)ptr=reinterpret_cast<GUID*>(std::uintptr_t(a[0]));else if(a[0]){id=guid(a[0]);ptr=&id;}IDirectDraw* object=nullptr;const auto hr=DirectDrawCreate(ptr,&object,nullptr);if(SUCCEEDED(hr))p.memory.store(a[1],wrap(object,Interface::draw1),32);return U32(hr);});
    }
public:
    explicit Draw(Process& q):p(q){install();}
    ~Draw() override{shutdown();}
    void shutdown() noexcept override{
        if(stopped)return;stopped=true;
        for(auto& [address,value]:objects){(void)address;if(value.native)destroy_lock(value);}
        for(auto kind:{Interface::surface7,Interface::unknown,Interface::draw1,Interface::draw7})for(auto& [address,value]:objects){(void)address;if(value.native && value.kind==kind){for(U32 n=0;n<value.guest_refs;++n)value.native->Release();value.native=nullptr;}}
    }
    std::string report() const override{std::ostringstream out;out<<"{\"backend\":\"native-ddraw7\",\"objects_created\":"<<created<<",\"objects_retired\":"<<retired<<",\"adapter_callbacks\":"<<enumerated<<",\"surface_locks\":"<<locks<<",\"surface_unlocks\":"<<unlocks<<",\"blits\":"<<blits<<"}";return out.str();}
};
U32 Draw::vtable(Interface kind){
    if(auto it=vtables.find(kind);it!=vtables.end())return it->second;
    const unsigned count=kind==Interface::unknown?3u:kind==Interface::draw1?23u:kind==Interface::draw7?30u:49u;
    Allocation table(p,count*4);
    for(unsigned slot=0;slot<count;++slot){
        const auto name=std::string("WinRecompCOM.")+std::to_string(unsigned(kind))+"."+std::to_string(slot);
        unsigned argc=1;Function fn=[this,kind,slot](Args a)->U32{object(a[0],kind);stop("unimplemented COM method "+std::to_string(unsigned(kind))+":"+std::to_string(slot));};
        auto method=[&](unsigned n,Function f){argc=n;fn=std::move(f);};
        if(slot==0)method(3,[this,kind](Args a){return query(object(a[0],kind),a);});
        else if(slot==1)method(1,[this,kind](Args a){auto& value=object(a[0],kind);if(value.guest_refs==0xffffffffu)stop("reference count overflow");const auto result=value.native->AddRef();++value.guest_refs;return result;});
        else if(slot==2)method(1,[this,kind](Args a){return release(object(a[0],kind));});
        if(kind==Interface::draw7){
            if(slot==11)method(3,[this](Args a){
                std::array<DDCAPS,2> caps{};std::array<DWORD,2> sizes{};
                for(unsigned n=0;n<2;++n)if(a[n+1]){sizes[n]=p.memory.load(a[n+1],32);if(sizes[n]!=sizeof(DDCAPS_DX3) && sizes[n]!=sizeof(DDCAPS_DX5) && sizes[n]!=sizeof(DDCAPS_DX6) && sizes[n]!=sizeof(DDCAPS_DX7))stop("GetCaps legacy structure size");p.memory.check(a[n+1],sizes[n],Memory::Write);caps[n].dwSize=sizes[n];}
                const auto hr=static_cast<IDirectDraw7*>(object(a[0],Interface::draw7).native)->GetCaps(a[1]?&caps[0]:nullptr,a[2]?&caps[1]:nullptr);
                if(SUCCEEDED(hr))for(unsigned n=0;n<2;++n)if(a[n+1])p.memory.copy_in(a[n+1],std::span(reinterpret_cast<const std::uint8_t*>(&caps[n]),sizes[n]));return U32(hr);
            });
            if(slot==6)method(4,[this](Args a){auto d=read_description(a[1]);p.memory.check(a[2],4,Memory::Write);if(a[3])stop("aggregated surfaces not supported");IDirectDrawSurface7* surface=nullptr;const auto hr=static_cast<IDirectDraw7*>(object(a[0],Interface::draw7).native)->CreateSurface(&d,&surface,nullptr);if(SUCCEEDED(hr))p.memory.store(a[2],wrap(surface,Interface::surface7),32);return U32(hr);});
            if(slot==12)method(2,[this](Args a){p.memory.check(a[1],DescSize,Memory::Read|Memory::Write);if(p.memory.load(a[1],32)!=DescSize)stop("GetDisplayMode description size");DDSURFACEDESC2 d{};d.dwSize=sizeof(d);auto hr=static_cast<IDirectDraw7*>(object(a[0],Interface::draw7).native)->GetDisplayMode(&d);if(SUCCEEDED(hr)){if(d.lpSurface)stop("GetDisplayMode returned a native pixel pointer");write_description(a[1],d);}return U32(hr);});
            if(slot==20)method(3,[this](Args a){if(a[2]!=DDSCL_NORMAL)stop("only normal/windowed cooperative mode is supported");auto window=reinterpret_cast<HWND>(p.gui()->native_window(a[1]));return U32(static_cast<IDirectDraw7*>(object(a[0],Interface::draw7).native)->SetCooperativeLevel(window,a[2]));});
            if(slot==26)method(1,[this](Args a){return U32(static_cast<IDirectDraw7*>(object(a[0],Interface::draw7).native)->TestCooperativeLevel());});
        }
        if(kind==Interface::surface7){
            if(slot==5)method(6,[this](Args a){auto& value=object(a[0],Interface::surface7);if(value.locked)stop("blit while surface is locked");if(a[1] || a[2] || a[3] || (a[4]&~U32(DDBLT_COLORFILL|DDBLT_WAIT)) || !(a[4]&DDBLT_COLORFILL))stop("only whole-surface color fill is supported");p.memory.check(a[5],100,Memory::Read);if(p.memory.load(a[5],32)!=100)stop("DDBLTFX must have the 100-byte x86 layout");DDBLTFX fx{};fx.dwSize=sizeof(fx);fx.dwFillColor=p.memory.load(a[5]+80,32);auto hr=static_cast<IDirectDrawSurface7*>(value.native)->Blt(nullptr,nullptr,nullptr,a[4],&fx);if(SUCCEEDED(hr))++blits;return U32(hr);});
            if(slot==22)method(2,[this](Args a){p.memory.check(a[1],DescSize,Memory::Read|Memory::Write);if(p.memory.load(a[1],32)!=DescSize)stop("GetSurfaceDesc description size");DDSURFACEDESC2 d{};d.dwSize=sizeof(d);auto hr=static_cast<IDirectDrawSurface7*>(object(a[0],Interface::surface7).native)->GetSurfaceDesc(&d);if(SUCCEEDED(hr)){if((d.dwFlags&DDSD_LPSURFACE) && d.lpSurface)stop("GetSurfaceDesc returned a live native pixel pointer; use Lock");write_description(a[1],d);}return U32(hr);});
            if(slot==24)method(1,[this](Args a){return U32(static_cast<IDirectDrawSurface7*>(object(a[0],Interface::surface7).native)->IsLost());});
            if(slot==25)method(5,[this](Args a){return lock(object(a[0],Interface::surface7),a);});
            if(slot==32)method(2,[this](Args a){return unlock(object(a[0],Interface::surface7),a);});
        }
        api(name.c_str(),argc,std::move(fn));p.memory.store(table.address+4*slot,p.resolve("ddraw.dll",name),32);
    }
    p.memory.protect(table.address,4096,Memory::Read);const auto result=table.address;vtables.emplace(kind,result);table.keep();return result;
}
}
std::unique_ptr<DirectDrawBackend> install_directdraw(Process& p){return std::make_unique<Draw>(p);}
#endif
}
