// The test-only dispatcher tail-calls the real default WNDPROC. It does not
// execute or interpret the original game, and is never linked into its EXE.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <d3d9.h>
#include <dsound.h>
#include <GL/gl.h>
#include <bit>
#include <fstream>
#include <iostream>
#include <sstream>
#include "support.hpp"
#include "wa2_win32.hpp"
#include "native_backend.hpp"
using wr::U32;
#define CHECK(value) do {if(!(value))throw std::runtime_error(#value);}while(false)
static wr::Process* active;
static bool callback_step(wr::Cpu& cpu,wr::Memory&,std::uint64_t&){
    if(cpu.eip!=active->entry)return false;cpu.eip=active->resolve("user32.dll","DefWindowProcA");return true;
}
int main(int argc,char** argv){try {
    if(argc<2 || argc>3 || (argc==3 && std::string(argv[2])!="--offscreen-only"))throw std::runtime_error("usage: wa2_graphics_tests original.exe [--offscreen-only]");
    const bool offscreen_only=argc==3;
    wr::ProcessOptions options;options.instruction_budget=1000000;
    wr::Process p(callback_step,options);active=&p;wr::install_wa2_win32(p);p.load(wr::Image::load(argv[1]));
    // This fixture deliberately supplies a synthetic callback dispatcher.
    // Do not let the whole-image AOT entries execute real game code instead.
    p.state().fast_step=nullptr;p.state().block_step=nullptr;p.state().fast_entries.clear();
    const auto capture=std::filesystem::absolute(argv[1]).parent_path()/"build"/"graphics-tests"/"frame.bmp";
    std::filesystem::create_directories(capture.parent_path());p.state().snapshot_path.clear();
    auto invoke=[&](U32 target,std::span<const U32> args){const auto saved=p.cpu;try{return p.callback(target,args);}catch(...){p.cpu=saved;throw;}};
    auto call=[&](const char* dll,const char* name,std::initializer_list<U32> args){const auto at=p.resolve(dll,name);CHECK(at);return invoke(at,std::span(args.begin(),args.size()));};
    auto com=[&](U32 object,unsigned slot,std::initializer_list<U32> args){std::vector<U32> values{object};values.insert(values.end(),args.begin(),args.end());return invoke(p.memory.load(p.memory.load(object,32)+slot*4,32),values);};
    auto native_com=[&](U32 object,unsigned slot,std::initializer_list<U32> args,bool expect_inline){
        const auto saved=p.cpu;struct Restore {wr::Process& p;wr::Cpu saved;~Restore(){p.cpu=saved;}} restore{p,saved};
        const auto target=p.memory.load(p.memory.load(object,32)+slot*4,32);constexpr U32 returned=0xffffffa0;
        // Execute only the original single RET into the real API binding.
        // This tests native bridges without running or capturing game scenes.
        std::vector<U32> words{target,returned,object};words.insert(words.end(),args.begin(),args.end());
        p.cpu.eip=0x43b050;p.cpu.r[wr::ESP]-=U32(words.size()*4);const auto sp=p.cpu.r[wr::ESP];
        for(unsigned n=0;n<words.size();++n)p.memory.store(sp+n*4,words[n],32);
        std::uint64_t budget=1000;const auto before=wr::wa2_native_inline_state_api_calls();
        CHECK(wr::wa2_native_batch(p.cpu,p.memory,budget,64,returned)==1);
        CHECK((wr::wa2_native_inline_state_api_calls()!=before)==expect_inline);
        if(p.cpu.eip==target){wr::tick(budget,p.cpu.eip);CHECK(p.dispatch_api());}
        CHECK(p.cpu.eip==returned && p.cpu.r[wr::ESP]==saved.r[wr::ESP] && budget==998);
        CHECK(p.cpu.flags==saved.flags && p.cpu.defined_flags==saved.defined_flags && p.cpu.fp.bytes==saved.fp.bytes);
        for(auto reg:{wr::ECX,wr::EDX,wr::EBX,wr::EBP,wr::ESI,wr::EDI})CHECK(p.cpu.r[reg]==saved.r[reg]);
        return p.cpu.r[wr::EAX];
    };
    auto ok=[&](U32 result){if(FAILED(HRESULT(result))){std::ostringstream out;out<<"D3D HRESULT 0x"<<std::hex<<result;throw std::runtime_error(out.str());}};
    const auto buffer=p.allocate_bytes(4096),klass=p.put_string("WA2 D3D boundary"),title=p.put_string("WA2 graphics conformance");
    wr::port::write(p,buffer,std::array<U32,10>{CS_OWNDC,p.entry,0,0,p.image_base,0,0,0,0,klass});
    CHECK(call("user32.dll","RegisterClassA",{buffer}));
    const auto window=call("user32.dll","CreateWindowExA",{0,klass,title,WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX,0,0,96,96,0,0,p.image_base,0});CHECK(window);
    const auto host_window=reinterpret_cast<HWND>(p.gui()->native_window(window));RECT initial_client{};CHECK(GetClientRect(host_window,&initial_client));
    CHECK(initial_client.right>0 && initial_client.bottom>0);
    const auto d3d=call("d3d9.dll","Direct3DCreate9",{D3D_SDK_VERSION});CHECK(d3d && com(d3d,4,{})>0);
    ok(com(d3d,14,{0,D3DDEVTYPE_HAL,buffer}));
    const U32 flags=D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE;
    wr::port::write(p,buffer,std::array<U32,14>{64,64,D3DFMT_A8R8G8B8,1,D3DMULTISAMPLE_NONE,0,D3DSWAPEFFECT_DISCARD,window,1,0,D3DFMT_UNKNOWN,0,0,D3DPRESENT_INTERVAL_IMMEDIATE});
    const auto prior_control=p.cpu.fp.get16(0);ok(com(d3d,16,{0,D3DDEVTYPE_HAL,window,flags,buffer,buffer+128}));
    // WA2 has WS_POPUP together with a title bar. SDL's foreign-window
    // borderless sizing must not replace its Win32 client area with 0x0.
    CHECK(call("user32.dll","SetWindowPos",{window,0,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_FRAMECHANGED}));
    RECT after_frame{};CHECK(GetClientRect(host_window,&after_frame));
    std::cerr<<"foreign captioned popup client area: "<<initial_client.right<<'x'<<initial_client.bottom<<" -> "<<after_frame.right<<'x'<<after_frame.bottom<<'\n';
    CHECK(after_frame.right==initial_client.right && after_frame.bottom==initial_client.bottom);
    CHECK(p.cpu.fp.get16(0)==prior_control);const auto device=p.memory.load(buffer+128,32);CHECK(device);
    ok(com(device,9,{buffer+256}));CHECK(p.memory.load(buffer+264,32)==window && p.memory.load(buffer+268,32)==flags);
    bool rejected=false;try{com(device,7,{0});}catch(const wr::GuestFault& e){rejected=e.kind==wr::FaultKind::memory;}CHECK(rejected);
    ok(call("d3dx9_41.dll","D3DXCreateTexture",{device,8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,buffer+128}));
    const auto texture=p.memory.load(buffer+128,32);CHECK(texture);
    ok(com(texture,17,{0,buffer+256}));CHECK(p.memory.load(buffer+280,32)==8 && p.memory.load(buffer+284,32)==8);
    ok(com(texture,19,{0,buffer+256,0,0}));auto pitch=p.memory.load(buffer+256,32),pixels=p.memory.load(buffer+260,32);
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)p.memory.store(pixels+y*pitch+x*4,0xff112233,32);
    ok(com(texture,20,{0}));ok(com(texture,19,{0,buffer+256,0,D3DLOCK_READONLY}));
    pitch=p.memory.load(buffer+256,32);pixels=p.memory.load(buffer+260,32);CHECK(p.memory.load(pixels+7*pitch+7*4,32)==0xff112233);
    rejected=false;try{p.memory.store(pixels,0,32);}catch(const wr::GuestFault& e){rejected=e.kind==wr::FaultKind::memory;}CHECK(rejected);ok(com(texture,20,{0}));
    ok(com(texture,18,{0,buffer+128}));const auto texture_surface=p.memory.load(buffer+128,32);
    const auto source=p.allocate_bytes(256);for(unsigned n=0;n<64;++n)p.memory.store(source+n*4,0xff00ff11,32);
    wr::port::write(p,buffer+256,RECT{0,0,8,8});
    ok(call("d3dx9_41.dll","D3DXLoadSurfaceFromMemory",{texture_surface,0,0,source,D3DFMT_A8R8G8B8,32,0,buffer+256,1,0}));
    ok(call("d3dx9_41.dll","D3DXCreateTexture",{device,8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,buffer+128}));
    const auto alternate_texture=p.memory.load(buffer+128,32);ok(com(alternate_texture,18,{0,buffer+128}));const auto alternate_surface=p.memory.load(buffer+128,32);
    const auto alternate_source=p.allocate_bytes(256);for(unsigned n=0;n<64;++n)p.memory.store(alternate_source+n*4,0xff0000ff,32);
    ok(call("d3dx9_41.dll","D3DXLoadSurfaceFromMemory",{alternate_surface,0,0,alternate_source,D3DFMT_A8R8G8B8,32,0,buffer+256,1,0}));
    ok(com(device,65,{0,texture}));ok(com(device,89,{D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1}));
    struct Vertex {float x,y,z,rhw;DWORD color;float u,v;};
    const auto vertices=p.allocate_bytes(4*sizeof(Vertex));
    wr::port::write(p,vertices,std::array<Vertex,4>{{{1,1,0,1,0xffffffff,0,0},{31,1,0,1,0xffffffff,1,0},{1,31,0,1,0xffffffff,0,1},{31,31,0,1,0xffffffff,1,1}}});
    auto draw=[&](){ok(com(device,41,{}));ok(com(device,43,{0,0,D3DCLEAR_TARGET,0xff102030,std::bit_cast<U32>(1.0f),0}));ok(com(device,83,{D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)}));ok(com(device,42,{}));};
    ok(com(device,18,{0,0,D3DBACKBUFFER_TYPE_MONO,buffer+128}));const auto back=p.memory.load(buffer+128,32);
    ok(com(device,36,{64,64,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,buffer+128,0}));const auto readback=p.memory.load(buffer+128,32);
    auto sample=[&](U32 surface,unsigned x,unsigned y){ok(com(device,32,{surface,readback}));ok(com(readback,13,{buffer+256,0,D3DLOCK_READONLY}));const auto stride=p.memory.load(buffer+256,32),at=p.memory.load(buffer+260,32);const auto value=p.memory.load(at+y*stride+x*4,32);ok(com(readback,14,{}));return value&0xffffffu;};
    auto pixel=[&](unsigned x,unsigned y){return sample(back,x,y);};
    draw();CHECK(pixel(0,0)==0x102030 && pixel(16,16)==0x00ff11);
    ok(native_com(device,89,{D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1},true));
    ok(native_com(device,65,{0,texture},true));ok(com(device,37,{0,back}));ok(native_com(device,37,{0,back},true));
    // Deferred native bindings are visible to the very next ordinary API,
    // keep resources alive through Release, and cancel before batched draws.
    ok(native_com(device,65,{0,alternate_texture},true));ok(com(device,64,{0,buffer+128}));
    const auto queried_texture=p.memory.load(buffer+128,32);CHECK(queried_texture==alternate_texture);com(queried_texture,2,{});
    draw();CHECK(pixel(16,16)==0x0000ff);
    ok(native_com(device,65,{0,0},true));ok(native_com(device,65,{0,texture},true));draw();CHECK(pixel(16,16)==0x00ff11);
    wr::port::write(p,buffer+256,D3DVIEWPORT9{0,0,32,32,0,1});ok(com(device,47,{buffer+256}));
    ok(native_com(device,37,{0,back},false));ok(com(device,48,{buffer+256}));CHECK(p.memory.load(buffer+264,32)==64 && p.memory.load(buffer+268,32)==64);
    ok(native_com(device,37,{0,back},true));
    const std::array<D3DVERTEXELEMENT9,2> declaration_elements{{{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITIONT,0},D3DDECL_END()}};
    wr::port::write(p,buffer+512,declaration_elements);ok(com(device,86,{buffer+512,buffer+128}));const auto declaration=p.memory.load(buffer+128,32);
    ok(com(device,87,{declaration}));ok(native_com(device,89,{0},false));ok(com(device,88,{buffer+128}));CHECK(!p.memory.load(buffer+128,32));
    ok(native_com(device,89,{0},true));ok(native_com(device,89,{D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1},false));com(declaration,2,{});
    ok(call("d3dx9_41.dll","D3DXCreateTexture",{device,8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,buffer+128}));
    const auto stale_texture=p.memory.load(buffer+128,32);ok(native_com(device,65,{0,stale_texture},true));CHECK(com(stale_texture,2,{})==0);
    bool stale_rejected=false;try{native_com(device,65,{0,stale_texture},false);}catch(const wr::GuestFault& e){stale_rejected=e.kind==wr::FaultKind::unsupported;}CHECK(stale_rejected);
    ok(com(device,65,{0,texture}));draw();CHECK(pixel(16,16)==0x00ff11);
    // Synthetic offscreen surfaces verify cache reuse and state changes. No
    // desktop/front-buffer read or game scene capture is needed for these.
    ok(com(device,41,{}));ok(com(device,83,{D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)}));
    ok(com(device,65,{0,0}));ok(com(device,65,{0,texture}));
    ok(com(device,83,{D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)}));
    const auto cached_report=p.report();const auto cache_field=cached_report.find("\"state_cache_hits\":");CHECK(cache_field!=std::string::npos);
    CHECK(std::stoull(cached_report.substr(cache_field+19))>0);
    ok(com(device,65,{0,alternate_texture}));ok(com(device,83,{D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)}));
    wr::port::write(p,vertices,std::array<Vertex,4>{{{33,1,0,1,0xffffffff,0,0},{63,1,0,1,0xffffffff,1,0},{33,31,0,1,0xffffffff,0,1},{63,31,0,1,0xffffffff,1,1}}});
    ok(com(device,65,{0,texture}));ok(com(device,83,{D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)}));
    CHECK(pixel(16,16)==0x0000ff && pixel(48,16)==0x00ff11);
    const auto texture_report=p.report();const auto texture_field=texture_report.find("\"texture_only_applies\":");CHECK(texture_field!=std::string::npos);
    CHECK(std::stoull(texture_report.substr(texture_field+23))>=2);
    wr::port::write(p,vertices,std::array<Vertex,4>{{{1,1,0,1,0xffffffff,0,0},{31,1,0,1,0xffffffff,1,0},{1,31,0,1,0xffffffff,0,1},{31,31,0,1,0xffffffff,1,1}}});
    ok(com(device,83,{D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)}));
    ok(native_com(device,57,{D3DRS_TEXTUREFACTOR,0xffff0000},true));
    ok(com(device,67,{0,D3DTSS_COLOROP,D3DTOP_SELECTARG1}));ok(com(device,67,{0,D3DTSS_COLORARG1,D3DTA_TFACTOR}));
    wr::port::write(p,buffer+256,RECT{8,8,24,24});ok(com(device,75,{buffer+256}));ok(com(device,57,{D3DRS_SCISSORTESTENABLE,1}));
    ok(com(device,83,{D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)}));ok(com(device,42,{}));
    CHECK(pixel(16,16)==0xff0000 && pixel(4,4)==0x00ff11);
    ok(com(device,57,{D3DRS_SCISSORTESTENABLE,0}));ok(com(device,57,{D3DRS_TEXTUREFACTOR,0xffffffff}));
    ok(com(device,67,{0,D3DTSS_COLOROP,D3DTOP_MODULATE}));ok(com(device,67,{0,D3DTSS_COLORARG1,D3DTA_TEXTURE}));
    const auto normal_vertices=wr::port::read<std::array<Vertex,4>>(p,vertices);auto outside_vertices=normal_vertices;for(auto& v:outside_vertices)v.u=v.v=-1;
    wr::port::write(p,vertices,outside_vertices);
    ok(com(device,69,{0,D3DSAMP_ADDRESSU,D3DTADDRESS_BORDER}));ok(com(device,69,{0,D3DSAMP_ADDRESSV,D3DTADDRESS_BORDER}));
    for(auto [selected,color]:{std::pair<U32,U32>{texture,0xffff0000},{texture,0xff0000ff},{alternate_texture,0xff0000ff},{texture,0xff00ff00}}){
        ok(com(device,65,{0,selected}));ok(com(device,69,{0,D3DSAMP_BORDERCOLOR,color}));draw();CHECK(pixel(16,16)==(color&0xffffff));
    }
    const auto sampler_report=p.report();const auto sampler_field=sampler_report.find("\"parameter_skips\":");CHECK(sampler_field!=std::string::npos && std::stoull(sampler_report.substr(sampler_field+18))>0);
    ok(com(device,69,{0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP}));ok(com(device,69,{0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP}));
    ok(com(device,69,{0,D3DSAMP_BORDERCOLOR,0}));wr::port::write(p,vertices,normal_vertices);draw();CHECK(pixel(16,16)==0x00ff11);
    // Check the window's default front buffer with diagnostics disabled. The
    // old tests only checked the offscreen D3D surface and its BMP snapshot.
    if(!offscreen_only){
    const auto native_window=reinterpret_cast<HWND>(p.gui()->native_window(window));
    ShowWindow(native_window,SW_SHOWNOACTIVATE);RECT client{};CHECK(GetClientRect(native_window,&client));
    ok(com(device,17,{0,0,0,0}));
    GLint prior_read=0;glGetIntegerv(GL_READ_BUFFER,&prior_read);glReadBuffer(GL_FRONT);glFinish();
    std::array<unsigned char,4> front_pixel{};
    glReadPixels(client.right*16/64,client.bottom-client.bottom*16/64-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,front_pixel.data());
    const auto front_error=glGetError();glReadBuffer(GLenum(prior_read));
    std::cerr<<"window front buffer without snapshot: "<<unsigned(front_pixel[0])<<','<<unsigned(front_pixel[1])<<','<<unsigned(front_pixel[2])<<" GL error="<<front_error<<'\n';
    CHECK(front_error==GL_NO_ERROR && front_pixel[0]==0 && front_pixel[1]==255 && front_pixel[2]==17);
    }
    const auto palette=p.allocate_bytes(1024),indexed=p.allocate_bytes(64);
    std::array<PALETTEENTRY,256> entries{};entries[1]={255,0,0,255};entries[2]={0,0,255,255};wr::port::write(p,palette,entries);
    // Compare non-square uploads to the installed native D3DX implementation.
    // The reference DLL is test-only; it is never linked into the game.
    {
        struct Module {HMODULE value;~Module(){if(value)::FreeLibrary(value);}} d3d_module{::LoadLibraryExW(L"d3d9.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)},d3dx_module{::LoadLibraryExW(L"d3dx9_41.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)};
        CHECK(d3d_module.value && d3dx_module.value);
        const auto create=reinterpret_cast<IDirect3D9* (WINAPI*)(UINT)>(::GetProcAddress(d3d_module.value,"Direct3DCreate9"));
        using Load=HRESULT (WINAPI*)(IDirect3DSurface9*,const PALETTEENTRY*,const RECT*,const void*,D3DFORMAT,UINT,const PALETTEENTRY*,const RECT*,DWORD,D3DCOLOR);
        const auto load=reinterpret_cast<Load>(::GetProcAddress(d3dx_module.value,"D3DXLoadSurfaceFromMemory"));CHECK(create && load);
        struct Native {HWND window{};IDirect3D9* d3d{};IDirect3DDevice9* device{};IDirect3DSurface9* surface{};~Native(){if(surface)surface->Release();if(device)device->Release();if(d3d)d3d->Release();if(window)::DestroyWindow(window);}} oracle;
        oracle.window=::CreateWindowExA(0,"STATIC","D3DX pixel reference",WS_OVERLAPPED,0,0,32,32,nullptr,nullptr,::GetModuleHandleW(nullptr),nullptr);CHECK(oracle.window);oracle.d3d=create(D3D_SDK_VERSION);CHECK(oracle.d3d);
        D3DPRESENT_PARAMETERS pp{};pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_UNKNOWN;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=oracle.window;pp.Windowed=TRUE;
        ok(U32(oracle.d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,oracle.window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&oracle.device)));
        ok(U32(oracle.device->CreateOffscreenPlainSurface(8,8,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&oracle.surface,nullptr)));
        const auto input=p.allocate_bytes(4096);std::array<U32,80> native_source{};for(unsigned n=0;n<native_source.size();++n)native_source[n]=0x80000000u|((n*29u&255)<<16)|((n*53u&255)<<8)|(n*101u&255);native_source[12]=0xff0044ff;wr::port::write(p,input,native_source);
        for(const U32 filter:{1u,2u})for(const auto area:{RECT{0,0,4,3},RECT{1,1,5,4},RECT{1,1,9,8}}){
            D3DLOCKED_RECT lock{};ok(U32(oracle.surface->LockRect(&lock,nullptr,0)));for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)reinterpret_cast<U32*>(static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch)[x]=0xcc884422u;ok(U32(oracle.surface->UnlockRect()));
            ok(com(texture,19,{0,buffer+256,0,0}));const auto at=p.memory.load(buffer+260,32),row=p.memory.load(buffer+256,32);for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)p.memory.store(at+y*row+x*4,0xcc884422,32);ok(com(texture,20,{0}));
            const RECT destination{1,2,7,7};wr::port::write(p,buffer+256,area);wr::port::write(p,buffer+288,destination);
            ok(U32(load(oracle.surface,nullptr,&destination,native_source.data(),D3DFMT_A8R8G8B8,40,nullptr,&area,filter,0xff0044ff)));
            ok(call("d3dx9_41.dll","D3DXLoadSurfaceFromMemory",{texture_surface,0,buffer+288,input,D3DFMT_A8R8G8B8,40,0,buffer+256,filter,0xff0044ff}));
            ok(U32(oracle.surface->LockRect(&lock,nullptr,D3DLOCK_READONLY)));ok(com(texture,19,{0,buffer+256,0,D3DLOCK_READONLY}));const auto pixels=p.memory.load(buffer+260,32),stride=p.memory.load(buffer+256,32);
            bool match=true;unsigned bad_x=0,bad_y=0;U32 expected=0,actual=0;
            for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){const auto value=reinterpret_cast<const U32*>(static_cast<const unsigned char*>(lock.pBits)+y*lock.Pitch)[x],got=p.memory.load(pixels+y*stride+x*4,32);if(value!=got && match){match=false;bad_x=x;bad_y=y;expected=value;actual=got;}}
            ok(com(texture,20,{0}));ok(U32(oracle.surface->UnlockRect()));
            if(!match){std::cerr<<"native D3DX mismatch filter="<<filter<<" source="<<area.right-area.left<<'x'<<area.bottom-area.top<<" pixel="<<bad_x<<','<<bad_y<<" expected="<<std::hex<<expected<<" actual="<<actual<<std::dec<<'\n';throw std::runtime_error("D3DX source/destination rectangles differ from native behavior");}
        }
        p.memory.release(input);std::cout<<"PASS native D3DX oracle: filter NONE/POINT, smaller/larger/cropped source, destination offset, padding and color key\n";
    }
    // An atlas tile starts on page two. The prefix is inaccessible and row
    // padding is unrelated image data; only the selected rectangle is read.
    const auto atlas=p.allocate_bytes(8192);p.memory.protect(atlas,4096,0);
    for(const auto format:{D3DFMT_A8R8G8B8,D3DFMT_P8,D3DFMT_A8P8}){
        const unsigned size=format==D3DFMT_P8?1:format==D3DFMT_A8P8?2:4;
        for(unsigned y=8;y<16;++y)for(unsigned x=8;x<16;++x){const auto at=atlas+y*512+x*size;if(format==D3DFMT_A8R8G8B8)p.memory.store(at,0x7fff0000,32);else if(format==D3DFMT_P8)p.memory.store(at,1,8);else p.memory.store(at,0x7f01,16);}
        wr::port::write(p,buffer+256,RECT{8,8,16,16});
        ok(call("d3dx9_41.dll","D3DXLoadSurfaceFromMemory",{texture_surface,0,0,atlas,U32(format),512,format==D3DFMT_A8R8G8B8?0:palette,buffer+256,1,0}));
        ok(com(texture,19,{0,buffer+256,0,D3DLOCK_READONLY}));const auto at=p.memory.load(buffer+260,32),stride=p.memory.load(buffer+256,32);
        CHECK(p.memory.load(at,32)==(format==D3DFMT_P8?0xffff0000u:0x7fff0000u));CHECK(p.memory.load(at+stride*7+28,32)==p.memory.load(at,32));ok(com(texture,20,{0}));
    }
    p.memory.release(atlas);
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)p.memory.store(indexed+y*8+x,y<4?1:2,8);
    wr::port::write(p,buffer+256,RECT{0,0,8,8});ok(call("d3dx9_41.dll","D3DXLoadSurfaceFromMemory",{texture_surface,0,0,indexed,D3DFMT_P8,8,palette,buffer+256,1,0}));
    draw();CHECK(pixel(16,4)==0xff0000 && pixel(16,28)==0x0000ff);
    ok(com(device,23,{64,64,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,buffer+128,0}));const auto rt_texture=p.memory.load(buffer+128,32);
    ok(com(rt_texture,18,{0,buffer+128}));const auto rt=p.memory.load(buffer+128,32);
    ok(com(device,37,{0,rt}));
    wr::port::write(p,vertices,std::array<Vertex,4>{{{0,0,0,1,0xffffffff,0,0},{64,0,0,1,0xffffffff,1,0},{0,64,0,1,0xffffffff,0,1},{64,64,0,1,0xffffffff,1,1}}});
    draw();CHECK(sample(rt,32,8)==0xff0000 && sample(rt,32,56)==0x0000ff);
    ok(com(device,37,{0,back}));ok(com(device,34,{rt,0,back,0,D3DTEXF_LINEAR}));CHECK(pixel(32,8)==0xff0000 && pixel(32,56)==0x0000ff);ok(com(device,65,{0,rt_texture}));
    wr::port::write(p,vertices,std::array<Vertex,4>{{{1,1,0,1,0xffffffff,0,0},{31,1,0,1,0xffffffff,1,0},{1,31,0,1,0xffffffff,0,1},{31,31,0,1,0xffffffff,1,1}}});
    draw();CHECK(pixel(16,4)==0xff0000 && pixel(16,28)==0x0000ff);
    ok(com(device,65,{0,texture}));com(rt,2,{});com(rt_texture,2,{});
    for(unsigned n=0;n<64;++n)p.memory.store(source+n*4,0x80ff0000,32);wr::port::write(p,buffer+256,RECT{0,0,8,8});
    ok(call("d3dx9_41.dll","D3DXLoadSurfaceFromMemory",{texture_surface,0,0,source,D3DFMT_A8R8G8B8,32,0,buffer+256,1,0}));
    ok(com(device,57,{D3DRS_ALPHABLENDENABLE,1}));ok(com(device,57,{D3DRS_SRCBLEND,D3DBLEND_SRCALPHA}));ok(com(device,57,{D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA}));
    draw();const auto mixed=pixel(16,16);CHECK(std::abs(int((mixed>>16)&255)-136)<=1 && std::abs(int((mixed>>8)&255)-16)<=1 && std::abs(int(mixed&255)-24)<=1);
    ok(com(device,41,{}));ok(com(device,43,{0,0,D3DCLEAR_TARGET,0,std::bit_cast<U32>(1.0f),0}));
    for(unsigned n=0;n<2;++n)ok(com(device,83,{D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)}));
    ok(com(device,42,{}));const auto twice=pixel(16,16);CHECK(std::abs(int((twice>>16)&255)-192)<=1 && !(twice&0xffff));
    const auto strip_vertices=wr::port::read<std::array<Vertex,4>>(p,vertices);
    wr::port::write(p,vertices,std::array<Vertex,4>{strip_vertices[0],strip_vertices[1],strip_vertices[3],strip_vertices[2]});
    ok(com(device,41,{}));ok(com(device,43,{0,0,D3DCLEAR_TARGET,0,std::bit_cast<U32>(1.0f),0}));
    ok(com(device,83,{D3DPT_TRIANGLEFAN,2,vertices,sizeof(Vertex)}));ok(com(device,42,{}));CHECK(std::abs(int((pixel(16,16)>>16)&255)-128)<=1);
    wr::port::write(p,vertices,strip_vertices);const auto triangle_indices=p.allocate_bytes(12);wr::port::write(p,triangle_indices,std::array<std::uint16_t,6>{0,1,2,2,1,3});
    ok(com(device,41,{}));ok(com(device,43,{0,0,D3DCLEAR_TARGET,0,std::bit_cast<U32>(1.0f),0}));
    ok(com(device,84,{D3DPT_TRIANGLELIST,0,4,2,triangle_indices,D3DFMT_INDEX16,vertices,sizeof(Vertex)}));ok(com(device,42,{}));
    CHECK(std::abs(int((pixel(16,16)>>16)&255)-128)<=1);
    ok(com(device,57,{D3DRS_ALPHABLENDENABLE,0}));
    const auto shader=p.put_string("ps_2_0\nmov oC0, c0\n");
    ok(call("d3dx9_41.dll","D3DXAssembleShader",{shader,U32(p.read_string(shader).size()),0,0,0,buffer+128,buffer+132}));
    const auto bytecode=p.memory.load(buffer+128,32);CHECK(bytecode && com(bytecode,4,{})>0);const auto code=com(bytecode,3,{});CHECK(code);
    ok(com(device,106,{code,buffer+128}));const auto pixel_shader=p.memory.load(buffer+128,32);
    ok(com(device,107,{pixel_shader}));wr::port::write(p,buffer+256,std::array<float,4>{1,0,0,1});ok(com(device,109,{0,buffer+256,1}));
    draw();CHECK(pixel(16,16)==0xff0000);if(!offscreen_only)p.state().snapshot_path=capture;ok(com(device,17,{0,0,0,0}));
    if(!offscreen_only)
    {std::ifstream captured(capture,std::ios::binary);BITMAPFILEHEADER h{};BITMAPINFOHEADER info{};captured.read(reinterpret_cast<char*>(&h),sizeof(h));captured.read(reinterpret_cast<char*>(&info),sizeof(info));CHECK(captured && h.bfType==0x4d42 && info.biWidth==64 && info.biHeight==-64 && info.biBitCount==32);captured.seekg(h.bfOffBits+16*64*4+16*4);U32 color=0;captured.read(reinterpret_cast<char*>(&color),4);CHECK(captured && (color&0xffffffu)==0xff0000);}
    auto assemble=[&](const std::string& text,unsigned slot){const auto at=p.put_string(text);ok(call("d3dx9_41.dll","D3DXAssembleShader",{at,U32(text.size()),0,0,0,buffer+128,buffer+132}));const auto b=p.memory.load(buffer+128,32),code=com(b,3,{});ok(com(device,slot,{code,buffer+128}));const auto result=p.memory.load(buffer+128,32);com(b,2,{});return result;};
    const auto textured_shader=assemble("ps_2_0\ndcl t0.xy\ndcl_2d s0\ntexld r0, t0, s0\nmul oC0, r0, c0\n",106);
    const auto vertex_shader=assemble("vs_2_0\ndcl_position v0\ndcl_texcoord v1\nmov oPos, v0\nmov oT0, v1\n",91);
    wr::port::write(p,buffer+256,RECT{0,0,8,8});ok(call("d3dx9_41.dll","D3DXLoadSurfaceFromMemory",{texture_surface,0,0,indexed,D3DFMT_P8,8,palette,buffer+256,1,0}));
    ok(com(device,107,{textured_shader}));ok(com(device,92,{vertex_shader}));ok(com(device,89,{D3DFVF_XYZ|D3DFVF_TEX1}));
    wr::port::write(p,buffer+256,std::array<float,4>{1,1,1,1});ok(com(device,109,{0,buffer+256,1}));
    struct ShaderVertex {float x,y,z,u,v;};const auto sv=p.allocate_bytes(4*sizeof(ShaderVertex));
    wr::port::write(p,sv,std::array<ShaderVertex,4>{{{-1,1,0,0,0},{1,1,0,1,0},{-1,-1,0,0,1},{1,-1,0,1,1}}});
    ok(com(device,41,{}));ok(com(device,43,{0,0,D3DCLEAR_TARGET,0,std::bit_cast<U32>(1.0f),0}));ok(com(device,83,{D3DPT_TRIANGLESTRIP,2,sv,sizeof(ShaderVertex)}));ok(com(device,42,{}));CHECK(pixel(32,8)==0xff0000 && pixel(32,56)==0x0000ff);
    ok(com(device,92,{0}));ok(com(device,107,{0}));com(vertex_shader,2,{});com(textured_shader,2,{});
    ok(com(device,65,{0,0}));ok(com(device,107,{0}));
    const auto number=[](const std::string& report,const char* field){
        const auto key='"'+std::string(field)+"\":";const auto at=report.find(key);CHECK(at!=std::string::npos);return std::stoull(report.substr(at+key.size()));
    };
    const auto regions_before_churn=p.memory.regions().size();
    wr::port::write(p,buffer+256,RECT{0,0,8,8});
    for(unsigned pass=0;pass<6;++pass){
        for(unsigned n=0;n<1000;++n){
            ok(call("d3dx9_41.dll","D3DXCreateTexture",{device,8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,buffer+128}));const auto t=p.memory.load(buffer+128,32);
            ok(com(t,18,{0,buffer+128}));const auto s=p.memory.load(buffer+128,32);
            ok(com(t,18,{0,buffer+128}));CHECK(p.memory.load(buffer+128,32)==s);CHECK(com(s,2,{})==1);
            ok(call("d3dx9_41.dll","D3DXLoadSurfaceFromMemory",{s,0,0,source,D3DFMT_A8R8G8B8,32,0,buffer+256,1,0}));
            CHECK(com(s,2,{})==0);CHECK(com(t,2,{})==0);
        }
        const auto full=p.report();const auto section=full.find("\"sdl_resource_lifetimes\":");CHECK(section!=std::string::npos);
        const auto lifetime=full.substr(section);const auto live=number(lifetime,"live_objects");
        CHECK(number(lifetime,"resource_entries")<=live+512);
        CHECK(number(lifetime,"texture_entries")<=live+256);
        CHECK(number(lifetime,"image_entries")<=live+256);
        CHECK(p.memory.regions().size()<=regions_before_churn);
    }
    std::cout<<"PASS 6000 offscreen texture/surface upload/release cycles; histories and guest reservations remain bounded\n";
    for(auto value:{pixel_shader,bytecode,alternate_surface,alternate_texture,texture_surface,texture,readback,back,device,d3d})com(value,2,{});
    ok(call("dsound.dll","#11",{0,buffer+128,0}));const auto sound=p.memory.load(buffer+128,32);CHECK(sound);
    ok(com(sound,6,{window,DSSCL_PRIORITY}));
    const WAVEFORMATEX pcm{WAVE_FORMAT_PCM,1,22050,44100,2,16,0};wr::port::write(p,buffer+512,pcm);
    const U32 audio_flags=DSBCAPS_CTRLVOLUME|DSBCAPS_CTRLPAN|DSBCAPS_CTRLFREQUENCY|DSBCAPS_GETCURRENTPOSITION2|DSBCAPS_CTRLPOSITIONNOTIFY;
    wr::port::write(p,buffer+256,std::array<U32,9>{36,audio_flags,4096,0,buffer+512,0,0,0,0});
    ok(com(sound,3,{buffer+256,buffer+128,0}));const auto audio=p.memory.load(buffer+128,32);CHECK(audio);
    ok(com(audio,15,{U32(DSBVOLUME_MIN)}));
    ok(com(audio,11,{4000,256,buffer+128,buffer+132,buffer+136,buffer+140,0}));
    auto first=p.memory.load(buffer+128,32),first_size=p.memory.load(buffer+132,32),second=p.memory.load(buffer+136,32),second_size=p.memory.load(buffer+140,32);
    CHECK(first_size==96 && second_size==160);
    for(U32 n=0;n<first_size;++n)p.memory.store(first+n,0x23,8);for(U32 n=0;n<second_size;++n)p.memory.store(second+n,0x45,8);
    ok(com(audio,19,{first,first_size,second,second_size}));
    ok(com(audio,11,{4000,256,buffer+128,buffer+132,buffer+136,buffer+140,0}));
    first=p.memory.load(buffer+128,32);second=p.memory.load(buffer+136,32);
    CHECK(p.memory.load(first,8)==0x23 && p.memory.load(second,8)==0x45);ok(com(audio,19,{first,first_size,second,second_size}));
    const auto audio_event=call("kernel32.dll","CreateEventA",{0,0,0,0});CHECK(audio_event);
    wr::port::write(p,buffer+768,IID_IDirectSoundNotify);ok(com(audio,0,{buffer+768,buffer+128}));const auto notify=p.memory.load(buffer+128,32);CHECK(notify);
    wr::port::write(p,buffer+800,std::array<U32,2>{512,audio_event});ok(com(notify,3,{1,buffer+800}));
    ok(com(audio,12,{0,0,DSBPLAY_LOOPING}));ok(com(audio,9,{buffer+256}));CHECK(p.memory.load(buffer+256,32)&DSBSTATUS_PLAYING);
    CHECK(call("kernel32.dll","WaitForSingleObject",{audio_event,1000})==WAIT_OBJECT_0);
    ok(com(audio,18,{}));ok(com(audio,9,{buffer+256}));CHECK(!(p.memory.load(buffer+256,32)&DSBSTATUS_PLAYING));
    com(notify,2,{});CHECK(call("kernel32.dll","CloseHandle",{audio_event}));com(audio,2,{});
    std::uint64_t prior_lifetime=0;
    for(unsigned n=0;n<256;++n){
        const U32 bytes=n&1?8192:4096;
        wr::port::write(p,buffer+256,std::array<U32,9>{36,audio_flags,bytes,0,buffer+512,0,0,0,0});
        ok(com(sound,3,{buffer+256,buffer+128,0}));const auto sound_buffer=p.memory.load(buffer+128,32);
        ok(com(sound_buffer,13,{0}));
        const auto audio_report=p.report();
        const auto position=audio_report.rfind("\"buffer_timing\":[");CHECK(position!=std::string::npos);
        const auto lifetime=audio_report.rfind("\"lifetime\":",audio_report.find("\"live_objects\":",position));CHECK(lifetime!=std::string::npos);
        const auto serial=std::stoull(audio_report.substr(lifetime+11));CHECK(serial>prior_lifetime);prior_lifetime=serial;
        const auto last=audio_report.find("\"bytes\":",lifetime);CHECK(last!=std::string::npos);CHECK(std::stoul(audio_report.substr(last+8))==bytes);
        CHECK(com(sound_buffer,2,{})==0);
    }
    const auto sound_history=p.report();CHECK(number(sound_history,"active_traces")==0 && number(sound_history,"retired_traces")==32);
    com(sound,2,{});
    std::cout<<"PASS 256 sound buffer lifetimes with reused addresses and changing sizes; retired history capped at 32\n";
    CHECK(p.report().find("\"backend\":\"sdl2-opengl\"")!=std::string::npos);
    {std::ofstream evidence(capture.parent_path()/"report.json",std::ios::trunc);evidence<<p.report()<<'\n';evidence.close();CHECK(evidence);}
    std::cout<<"PASS SDL2/OpenGL graphics: x86 COM ABI, checked locks, P8 palette alpha, texture orientation, render-to-texture, alpha blend, assembled vertex/pixel shaders, draw/readback colors, Present\n";
    std::cout<<"PASS native audio: ordinal import, DirectSound8 device, PCM secondary buffer, wrapped two-part Lock/Unlock, native position notification/event wake, silent Play/Stop\n";
    std::cout<<p.report()<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
