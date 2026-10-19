// D3D9 is only the guest ABI. All graphics resources belong to SDL2/OpenGL.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define SDL_MAIN_HANDLED
#include <windows.h>
#include <d3d9.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#include <mojoshader.h>
#include <mojoshader_assembler.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <chrono>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <iomanip>
#include <sstream>
#include "support.hpp"
#include "wa2_win32.hpp"
#include "native_state_apis.hpp"
#include "weak_registry.hpp"
extern "C" MOJOSHADER_parseData WA2_assembler_MOJOSHADER_out_of_mem_data;
namespace wr { namespace {
using namespace port;
enum class Kind {unknown,d3d,device,texture,surface,vertexbuffer,indexbuffer,vertexshader,pixelshader,declaration,stateblock,swapchain,buffer,base_texture,resource};
#include "d3d9_tables.inc"
constexpr std::size_t MaxBytes=128u*1024*1024;
float real(U32 v){return std::bit_cast<float>(v);}
struct GL {
    PFNGLACTIVETEXTUREPROC ActiveTexture{};void (APIENTRY *MultiTexCoord4f)(GLenum,GLfloat,GLfloat,GLfloat,GLfloat){};
    void (APIENTRY *ClientActiveTexture)(GLenum){};
    PFNGLBLENDFUNCSEPARATEPROC BlendFuncSeparate{};PFNGLBLENDEQUATIONSEPARATEPROC BlendEquationSeparate{};PFNGLBLENDCOLORPROC BlendColor{};
    PFNGLGENFRAMEBUFFERSPROC GenFramebuffers{};PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers{};
    PFNGLBINDFRAMEBUFFERPROC BindFramebuffer{};PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D{};PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus{};
    PFNGLBLITFRAMEBUFFERPROC BlitFramebuffer{};
    template<class T>void load(T& fn,const char* name){fn=reinterpret_cast<T>(SDL_GL_GetProcAddress(name));if(!fn)throw std::runtime_error(std::string("OpenGL entry unavailable: ")+name);}
    void init(){
#define WA2_GL_LOAD(n) load(n,"gl" #n)
        WA2_GL_LOAD(ActiveTexture);WA2_GL_LOAD(MultiTexCoord4f);WA2_GL_LOAD(ClientActiveTexture);WA2_GL_LOAD(BlendFuncSeparate);WA2_GL_LOAD(BlendEquationSeparate);WA2_GL_LOAD(BlendColor);
        WA2_GL_LOAD(GenFramebuffers);WA2_GL_LOAD(DeleteFramebuffers);WA2_GL_LOAD(BindFramebuffer);WA2_GL_LOAD(FramebufferTexture2D);WA2_GL_LOAD(CheckFramebufferStatus);
        WA2_GL_LOAD(BlitFramebuffer);
#undef WA2_GL_LOAD
    }
};
struct Texture {
    struct Parameters {GLint s,t,min,mag;U32 border;};
    GLuint id{};std::optional<Parameters> parameters;
    std::function<void()> cleanup;~Texture(){if(cleanup)cleanup();}
};
struct Resource;using Res=std::shared_ptr<Resource>;
struct ImageData {
    U32 width{},height{},usage{},pool{},level{};D3DFORMAT format{};std::shared_ptr<Texture> texture;
    GLuint fbo{},attached_depth{};bool gpu_dirty{},cpu_dirty{true};std::vector<std::uint8_t> bytes;
    std::function<void()> cleanup;~ImageData(){if(cleanup)cleanup();}
    std::weak_ptr<Resource> surface;
};
struct State {
    std::array<U32,256> render{};
    std::array<std::array<U32,33>,8> stage{};std::array<std::array<U32,14>,16> sampler{};std::array<Res,16> textures{};
    Res target,depth,vertex_shader,pixel_shader,declaration,indices;
    struct Stream {Res buffer;U32 offset{},stride{},frequency{1};};std::array<Stream,16> streams{};
    std::map<U32,D3DMATRIX> transforms;
    std::array<std::array<float,4>,256> vertex_f{},pixel_f{};
    std::array<std::array<int,4>,16> vertex_i{},pixel_i{};std::array<int,16> vertex_b{},pixel_b{};
    D3DVIEWPORT9 viewport{};RECT scissor{};U32 fvf{};bool scene{};
    bool same_fixed_parameters(const State& other) const {
        if(vertex_shader || pixel_shader || other.vertex_shader || other.pixel_shader ||
           target!=other.target || depth!=other.depth || fvf!=other.fvf || render!=other.render ||
           stage!=other.stage || sampler!=other.sampler ||
           std::memcmp(&viewport,&other.viewport,sizeof(viewport)) || std::memcmp(&scissor,&other.scissor,sizeof(scissor)) ||
           transforms.size()!=other.transforms.size())return false;
        auto previous=other.transforms.begin();
        for(const auto& [key,matrix]:transforms){if(key!=previous->first || std::memcmp(&matrix,&previous->second,sizeof(matrix)))return false;++previous;}
        return true;
    }
};
struct Resource {
    Kind kind{};std::weak_ptr<Resource> device,container;U32 usage{},pool{},format{},fvf{},priority{},lod{};
    U32 data{};std::vector<std::uint8_t> bytes;std::vector<std::shared_ptr<ImageData>> levels;
    std::vector<D3DVERTEXELEMENT9> elements;MOJOSHADER_glShader* shader{};
    std::shared_ptr<State> saved;std::function<void()> cleanup;~Resource(){if(cleanup)cleanup();}
};
struct Lock {U32 guest{},pitch{},rows{},row_bytes{},offset{};RECT area{};bool readonly{};};
struct Object {Res resource;U32 refs{1};std::map<U32,Lock> locks;};
struct FixedVertex {std::array<float,4> pos,color;std::array<std::array<float,4>,8> uv;};
struct DecodedVertex {
    std::array<float,4> pos{0,0,0,1},color{1,1,1,1},specular{},normal{0,0,1,0};
    std::array<std::array<float,4>,8> uv;
    DecodedVertex(){for(auto& t:uv)t={0,0,0,1};}
};
class Graphics {
    Process& p;GL gl;SDL_Window* window{};SDL_GLContext context{};MOJOSHADER_glContext* mojo{};
    HWND host_window{};WNDPROC host_procedure{},sdl_procedure{};bool layout_hooked{};
    static constexpr auto LayoutProperty=L"WinRecomp.WA2.SDLLayout";
    static LRESULT CALLBACK window_procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) noexcept {
        const auto* self=reinterpret_cast<Graphics*>(::GetPropW(hwnd,LayoutProperty));
        if(!self)return ::DefWindowProcA(hwnd,message,wp,lp);
        // The existing Win32 window owns its non-client layout. SDL treats
        // WS_POPUP as borderless, including WA2's captioned popup, and its
        // foreign-window windowed-size cache can still be zero. Let the
        // original procedure compute layout; keep SDL's other event handling.
        const auto target=message==WM_NCCALCSIZE || message==WM_GETMINMAXINFO?self->host_procedure:self->sdl_procedure;
        return ::CallWindowProcA(target,hwnd,message,wp,lp);
    }
    void hook_layout(){
        if(!::SetPropW(host_window,LayoutProperty,this))stop("cannot attach foreign-window layout owner");
        ::SetLastError(0);sdl_procedure=reinterpret_cast<WNDPROC>(::SetWindowLongPtrA(host_window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(window_procedure)));
        if(!sdl_procedure){::RemovePropW(host_window,LayoutProperty);stop("cannot delegate foreign-window layout");}layout_hooked=true;
    }
    void unhook_layout() noexcept {
        if(!host_window)return;
        if(layout_hooked && ::IsWindow(host_window) && reinterpret_cast<WNDPROC>(::GetWindowLongPtrA(host_window,GWLP_WNDPROC))==window_procedure)
            ::SetWindowLongPtrA(host_window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(sdl_procedure));
        ::RemovePropW(host_window,LayoutProperty);layout_hooked=false;host_window=nullptr;
    }
    std::map<U32,Object> objects;std::map<Kind,U32> vtables;WeakRegistry<Resource> resources;
    WeakRegistry<Texture> textures_alive;WeakRegistry<ImageData> images;
    Res root,device_resource,backbuffer;State state;std::optional<State> recording,applied;
    std::shared_ptr<Wa2StateApiTable> state_apis;
    std::array<U32,14> presentation{};U32 focus_window{},creation_flags{};bool stopped{},initialized{};
    DWORD thread{::GetCurrentThreadId()};GLint max_texture{2048};
    std::uint64_t created{},devices{},textures{},presents{},draws{},uploads{},captures{},last_capture{};
    std::uint64_t state_applies{},state_cache_hits{},framebuffer_checks{},texture_only_applies{};
    std::uint64_t sampler_parameter_calls{},sampler_parameter_skips{},frame_error_checks{};
    bool sampler_cache_enabled{[]{wchar_t value[2]{};return ::GetEnvironmentVariableW(L"WA2_NO_SAMPLER_CACHE",value,2)!=1 || value[0]!=L'1';}()};
    int requested_swap_interval{},actual_swap_interval{},swap_interval_result{};
    std::vector<FixedVertex> pending_vertices;Res pending_target;
    // Graphics methods stay on one owner thread and do not call guest code.
    // Queued draws own their FixedVertex copies, so these staging buffers can
    // retain capacity across successive DrawPrimitiveUP calls.
    std::vector<std::uint8_t> up_vertex_bytes;
    std::vector<DecodedVertex> decoded_vertices;
    std::array<bool,8> pending_coordinates{},client_coordinates{};
    std::array<unsigned,8> pending_coordinate_indices{};bool client_arrays_valid{};
    std::uint64_t submitted_draws{},batched_guest_draws{},submitted_vertices{};
    std::uint64_t config_version{1},texture_version{1},applied_config_version{},applied_texture_version{};
    struct StateWords {std::array<U32,256> render{};std::array<std::array<U32,33>,8> stage{};std::array<std::array<U32,14>,16> sampler{};} baseline_words;
    std::array<Res,16> baseline_textures{};std::uint64_t word_differences{},texture_differences{};
    U32 device_address{},device_table{};
    std::array<U32,2> same_target{0xffffffffu,1};std::array<U32,16> same_textures{};
    std::array<U32,16> committed_textures{};
    std::array<U32,65536> live_texture_tokens{};
    std::uint64_t native_texture_version{},committed_texture_version{},native_texture_syncs{};
    std::string driver_vendor,driver_renderer,driver_version;
    std::map<std::string,std::uint64_t> calls;HRESULT capture_result{};
    std::uint64_t started_ms{::GetTickCount64()};
    std::map<std::uint64_t,std::uint64_t> frame_times_ms;
    struct PresentedFrame {std::uint64_t frame{},counter{};bool foreground{},minimized{};};
    std::deque<PresentedFrame> recent_frames;
    std::uint64_t clock_frequency{[]{LARGE_INTEGER value{};::QueryPerformanceFrequency(&value);return std::uint64_t(value.QuadPart);}()};
    // Opt-in numerical tracing of our own submission path. Never reads pixels.
    struct FrameTrace {
        bool enabled{[]{wchar_t value[2]{};return ::GetEnvironmentVariableW(L"WA2_FRAME_TRACE",value,2)==1 && value[0]==L'1';}()};
        std::uint64_t draw{},apply{},flush{},upload{},previous_counter{};
        struct Sample {std::uint64_t frame,counter,begin,draw,apply,flush,upload,copy,present,error,swap,draws,uploads;};
        std::deque<Sample> recent;
        struct Opacity {std::uint64_t frame,counter;U32 texture,width,height,alpha,factor;float left,top,right,bottom;};
        std::deque<Opacity> opacity;
        std::map<U32,std::pair<U32,U32>> last_opacity;
        std::uint64_t last_copy{},last_draws{},last_uploads{};
        static std::uint64_t now(){return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
        static std::uint64_t counter(){LARGE_INTEGER value{};::QueryPerformanceCounter(&value);return std::uint64_t(value.QuadPart);}
        struct Timer {
            std::uint64_t* value{};std::uint64_t start{};
            explicit Timer(std::uint64_t* destination):value(destination){if(value)start=now();}
            ~Timer(){if(value)*value+=now()-start;}
        };
        std::string report() const {
            std::ostringstream out;out<<"{\"enabled\":"<<(enabled?"true":"false")<<",\"counter_anchor\":"<<counter()<<",\"steady_anchor_ns\":"<<now()<<",\"frame_columns\":[\"frame\",\"counter\",\"present_begin_counter\",\"draw_ns\",\"apply_ns\",\"flush_ns\",\"upload_ns\",\"surface_copy_ns\",\"present_ns\",\"error_check_ns\",\"swap_ns\",\"draws\",\"uploads\"],\"recent\":[";
            bool first=true;for(const auto& v:recent){if(!first)out<<',';first=false;out<<'['<<v.frame<<','<<v.counter<<','<<v.begin<<','<<v.draw<<','<<v.apply<<','<<v.flush<<','<<v.upload<<','<<v.copy<<','<<v.present<<','<<v.error<<','<<v.swap<<','<<v.draws<<','<<v.uploads<<']';}
            out<<"],\"opacity_columns\":[\"frame\",\"counter\",\"texture\",\"width\",\"height\",\"vertex_alpha\",\"texture_factor\",\"left\",\"top\",\"right\",\"bottom\"],\"opacity\":[";
            first=true;for(const auto& v:opacity){if(!first)out<<',';first=false;out<<'['<<v.frame<<','<<v.counter<<','<<v.texture<<','<<v.width<<','<<v.height<<','<<v.alpha<<','<<v.factor<<','<<v.left<<','<<v.top<<','<<v.right<<','<<v.bottom<<']';}out<<"],\"screen_capture\":false}";return out.str();
        }
    } frame_trace;
    struct CopySample {std::uint64_t calls{},pixels{},total_ns{},readback_ns{};};
    std::map<std::string,CopySample> copy_samples;
    CopySample copy_total;
    std::uint64_t source_bytes_before_crop{},source_bytes_staged{};
    struct CopyTimer {
        CopySample* sample{};CopySample* total{};std::chrono::steady_clock::time_point start{};
        CopyTimer(CopySample* value,CopySample* overall):sample(value),total(overall),start(std::chrono::steady_clock::now()){}
        ~CopyTimer(){const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();total->total_ns+=ns;if(sample)sample->total_ns+=ns;}
    };
    RECT last_blit_source{},last_blit_destination{};U32 last_source_width{},last_source_height{},last_destination_width{},last_destination_height{};
    std::map<std::pair<MOJOSHADER_glShader*,MOJOSHADER_glShader*>,MOJOSHADER_glProgram*> programs;
    [[noreturn]]void stop(const std::string& s){throw GuestFault(FaultKind::unsupported,p.cpu.eip,"SDL2/OpenGL: "+s);}
    void enter(bool flush_pending=true){if(stopped || ::GetCurrentThreadId()!=thread)stop("foreign thread or stopped backend");sync_native_textures();if(context){if(SDL_GL_MakeCurrent(window,context))stop(SDL_GetError());MOJOSHADER_glMakeContextCurrent(mojo);}if(flush_pending)flush();}
    Object& object(U32 at){auto it=objects.find(at);if(it==objects.end() || !it->second.refs)stop("stale guest COM object");return it->second;}
    Res get(U32 at,Kind kind,bool null_ok=false){if(!at && null_ok)return {};auto r=object(at).resource;if(r->kind!=kind)stop("wrong guest interface");return r;}
    template<class T>std::vector<T> array(U32 at,std::size_t count){if(count>MaxBytes/sizeof(T))stop("staging limit");std::vector<T> v(count);if(count)p.memory.copy_out(at,std::span(reinterpret_cast<std::uint8_t*>(v.data()),count*sizeof(T)));return v;}
    template<class T>U32 output(U32 at,const T& v){write(p,at,v);return 0;}
    U32 out_res(U32 at,const Res& r){p.memory.check(at,4,Memory::Write);p.memory.store(at,r?wrap(r):0,32);return 0;}
    Res make(Kind kind){auto r=std::make_shared<Resource>();r->kind=kind;r->device=device_resource;resources.add(r);return r;}
    U32 vtable(Kind kind){if(auto it=vtables.find(kind);it!=vtables.end())return it->second;const auto table=methods(kind);auto at=p.allocate_bytes(table.size()*4);
        for(unsigned slot=0;slot<table.size();++slot){auto name="COM."+std::to_string(unsigned(kind))+"."+table[slot].name;
            auto* count=&calls[std::to_string(unsigned(kind))+"."+table[slot].name];
            api(p,"wa2.d3d9",name,table[slot].arguments,[this,kind,slot,count](Process&,Args a){enter(false);auto& o=object(a[0]);if(o.resource->kind!=kind)stop("wrong vtable");++*count;return method(o,slot,a);});p.memory.store(at+slot*4,p.resolve("wa2.d3d9",name),32);}
        p.memory.protect(at,4096,Memory::Read);vtables.emplace(kind,at);return at;}
    U32 wrap(const Res& r){
        for(auto& [at,o]:objects)if(o.resource==r){++o.refs;return at;}
        if(objects.size()>=8192)stop("live COM object limit");
        auto table=vtable(r->kind),at=p.allocate_bytes(4);p.memory.store(at,table,32);p.memory.protect(at,4096,Memory::Read);
        objects.emplace(at,Object{r,1,{}});++created;
        if(r->kind==Kind::texture){if(at&0xffffu)stop("unaligned internal texture token");live_texture_tokens[at>>16]=at;}
        if(r->kind==Kind::device){
            device_address=at;device_table=table;
            static_assert(D3DRS_LIGHTING==137 && D3DRS_FOGENABLE==28 && D3DRS_STENCILENABLE==52);
            static_assert(sizeof(state.stage)==8*33*sizeof(U32) && sizeof(state.sampler)==16*14*sizeof(U32));
            const auto bind=[&](unsigned slot,Wa2StateApiKind kind,U32* values,U32 rows,U32 columns){
                auto& count=calls["2."+std::string(methods_device[slot].name)];
                const auto* baseline=kind==Wa2StateApiKind::render?baseline_words.render.data():kind==Wa2StateApiKind::stage?baseline_words.stage[0].data():baseline_words.sampler[0].data();
                wa2_bind_state_api(*state_apis,p.memory.load(table+slot*4,32),at,kind,values,rows,columns,&count,&word_differences,baseline);
            };
            bind(57,Wa2StateApiKind::render,state.render.data(),1,256);
            bind(67,Wa2StateApiKind::stage,state.stage[0].data(),8,33);
            bind(69,Wa2StateApiKind::sampler,state.sampler[0].data(),16,14);
            refresh_same_states();
        }
        return at;
    }
    static unsigned bpp(D3DFORMAT f){switch(f){case D3DFMT_A8R8G8B8:case D3DFMT_X8R8G8B8:case D3DFMT_A8B8G8R8:case D3DFMT_X8B8G8R8:return 4;case D3DFMT_R8G8B8:return 3;case D3DFMT_R5G6B5:case D3DFMT_X1R5G5B5:case D3DFMT_A1R5G5B5:case D3DFMT_A4R4G4B4:case D3DFMT_A8L8:return 2;case D3DFMT_A8:case D3DFMT_L8:return 1;default:return 0;}}
    static bool depth_format(D3DFORMAT f){return f==D3DFMT_D16 || f==D3DFMT_D24S8 || f==D3DFMT_D24X8;}
    static std::array<std::uint8_t,4> decode(const std::uint8_t* q,D3DFORMAT f){
        U32 v=q[0];if(bpp(f)>=2)v|=U32(q[1])<<8;auto scale=[](U32 x,U32 max){return std::uint8_t((x*255+max/2)/max);};
        switch(f){case D3DFMT_A8R8G8B8:case D3DFMT_X8R8G8B8:return {q[2],q[1],q[0],f==D3DFMT_A8R8G8B8?q[3]:std::uint8_t(255)};
        case D3DFMT_A8B8G8R8:case D3DFMT_X8B8G8R8:return {q[0],q[1],q[2],f==D3DFMT_A8B8G8R8?q[3]:std::uint8_t(255)};
        case D3DFMT_R8G8B8:return {q[2],q[1],q[0],255};case D3DFMT_R5G6B5:return {scale((v>>11)&31,31),scale((v>>5)&63,63),scale(v&31,31),255};
        case D3DFMT_A1R5G5B5:case D3DFMT_X1R5G5B5:return {scale((v>>10)&31,31),scale((v>>5)&31,31),scale(v&31,31),std::uint8_t(f==D3DFMT_X1R5G5B5 || (v&0x8000)?255:0)};
        case D3DFMT_A4R4G4B4:return {scale((v>>8)&15,15),scale((v>>4)&15,15),scale(v&15,15),scale(v>>12,15)};
        case D3DFMT_A8:return {255,255,255,q[0]};case D3DFMT_L8:return {q[0],q[0],q[0],255};case D3DFMT_A8L8:return {q[0],q[0],q[0],q[1]};default:return {};}
    }
    static void encode(std::uint8_t* q,D3DFORMAT f,const std::array<std::uint8_t,4>& c){
        U32 v=0;switch(f){case D3DFMT_A8R8G8B8:case D3DFMT_X8R8G8B8:q[0]=c[2];q[1]=c[1];q[2]=c[0];q[3]=f==D3DFMT_A8R8G8B8?c[3]:255;return;
        case D3DFMT_A8B8G8R8:case D3DFMT_X8B8G8R8:q[0]=c[0];q[1]=c[1];q[2]=c[2];q[3]=f==D3DFMT_A8B8G8R8?c[3]:255;return;
        case D3DFMT_R8G8B8:q[0]=c[2];q[1]=c[1];q[2]=c[0];return;case D3DFMT_R5G6B5:v=(c[0]>>3)<<11|(c[1]>>2)<<5|(c[2]>>3);break;
        case D3DFMT_A1R5G5B5:case D3DFMT_X1R5G5B5:v=(c[0]>>3)<<10|(c[1]>>3)<<5|(c[2]>>3)|((f==D3DFMT_X1R5G5B5 || c[3]>=128)?0x8000:0);break;
        case D3DFMT_A4R4G4B4:v=(c[3]>>4)<<12|(c[0]>>4)<<8|(c[1]>>4)<<4|(c[2]>>4);break;
        case D3DFMT_A8:q[0]=c[3];return;case D3DFMT_L8:q[0]=c[0];return;case D3DFMT_A8L8:q[0]=c[0];q[1]=c[3];return;default:return;}
        q[0]=std::uint8_t(v);q[1]=std::uint8_t(v>>8);
    }
    std::shared_ptr<Texture> new_texture(){auto t=std::make_shared<Texture>();glGenTextures(1,&t->id);const auto id=t->id;t->cleanup=[id]{glDeleteTextures(1,&id);};textures_alive.add(t);return t;}
    std::shared_ptr<ImageData> new_image(U32 w,U32 h,D3DFORMAT f,U32 usage,U32 pool,std::shared_ptr<Texture> t={},U32 level=0){
        applied.reset();
        const auto bytes=std::uint64_t(w)*h*(depth_format(f)?4:bpp(f));if(!w || !h || w>U32(max_texture) || h>U32(max_texture) || !bytes || bytes>MaxBytes)stop("unsupported image size/format "+std::to_string(U32(f)));
        auto i=std::make_shared<ImageData>();i->width=w;i->height=h;i->format=f;i->usage=usage;i->pool=pool;i->level=level;i->bytes.resize(std::size_t(bytes));i->texture=t?t:new_texture();
        gl.ActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,i->texture->id);
        if(depth_format(f))glTexImage2D(GL_TEXTURE_2D,level,GL_DEPTH24_STENCIL8,w,h,0,GL_DEPTH_STENCIL,GL_UNSIGNED_INT_24_8,nullptr);
        else glTexImage2D(GL_TEXTURE_2D,level,(f==D3DFMT_X8R8G8B8 || f==D3DFMT_X8B8G8R8 || f==D3DFMT_R8G8B8)?GL_RGB8:GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);i->texture->parameters.reset();images.add(i);return i;
    }
    void fbo(const std::shared_ptr<ImageData>& i){
        applied.reset();
        if(!i->fbo){gl.GenFramebuffers(1,&i->fbo);const auto id=i->fbo;i->cleanup=[this,id]{gl.DeleteFramebuffers(1,&id);};
            gl.BindFramebuffer(GL_FRAMEBUFFER,i->fbo);gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,i->texture->id,i->level);
            ++framebuffer_checks;if(gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)stop("incomplete framebuffer");
        }else gl.BindFramebuffer(GL_FRAMEBUFFER,i->fbo);
    }
    void upload(const std::shared_ptr<ImageData>& i){
        if(!i->cpu_dirty || depth_format(i->format))return;
        FrameTrace::Timer timing(frame_trace.enabled?&frame_trace.upload:nullptr);
        const bool bgra=i->format==D3DFMT_A8R8G8B8 || i->format==D3DFMT_X8R8G8B8;
        const bool rgba32=i->format==D3DFMT_A8B8G8R8 || i->format==D3DFMT_X8B8G8R8;
        std::vector<std::uint8_t> rgba;const auto* data=i->bytes.data();
        if(!bgra && !rgba32){rgba.resize(std::size_t(i->width)*i->height*4);const auto size=bpp(i->format);for(std::size_t n=0;n<rgba.size()/4;++n){const auto c=decode(data+n*size,i->format);std::copy(c.begin(),c.end(),rgba.data()+n*4);}data=rgba.data();}
        GLint active=0,binding=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);gl.ActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);glBindTexture(GL_TEXTURE_2D,i->texture->id);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexSubImage2D(GL_TEXTURE_2D,i->level,0,0,i->width,i->height,bgra?GL_BGRA:GL_RGBA,GL_UNSIGNED_BYTE,data);
        glBindTexture(GL_TEXTURE_2D,GLuint(binding));gl.ActiveTexture(GLenum(active));i->cpu_dirty=false;
    }
    void download(const std::shared_ptr<ImageData>& i){if(!i->gpu_dirty)return;fbo(i);std::vector<std::uint8_t> rgba(std::size_t(i->width)*i->height*4);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,i->width,i->height,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());const auto size=bpp(i->format);for(std::size_t n=0;n<rgba.size()/4;++n)encode(i->bytes.data()+n*size,i->format,{rgba[n*4],rgba[n*4+1],rgba[n*4+2],rgba[n*4+3]});i->gpu_dirty=false;i->cpu_dirty=false;}
    std::shared_ptr<ImageData> image(const Res& r,U32 level=0){if(!r || (r->kind!=Kind::texture && r->kind!=Kind::surface) || level>=r->levels.size())stop("invalid image level");return r->levels[level];}
    Res surface(const std::shared_ptr<ImageData>& i,Res parent={}){if(auto r=i->surface.lock())return r;auto r=make(Kind::surface);r->levels={i};r->container=parent;i->surface=r;return r;}
    D3DSURFACE_DESC description(const std::shared_ptr<ImageData>& i){return {i->format,D3DRTYPE_SURFACE,i->usage,D3DPOOL(i->pool),D3DMULTISAMPLE_NONE,0,i->width,i->height};}
    Res texture(U32 w,U32 h,U32 levels,U32 usage,D3DFORMAT format,U32 pool){if(!context)stop("texture before device");if(!bpp(format))stop("unsupported texture format "+std::to_string(U32(format)));auto r=make(Kind::texture);r->usage=usage;r->pool=pool;r->format=format;auto t=new_texture();U32 max=1;for(U32 n=std::max(w,h);n>1;n>>=1)++max;if(!levels)levels=max;if(levels>max)stop("invalid mip count");for(U32 n=0;n<levels;++n)r->levels.push_back(new_image(std::max(1u,w>>n),std::max(1u,h>>n),format,usage,pool,t,n));glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,GLint(levels-1));++textures;return r;}
    RECT rectangle(U32 at,const std::shared_ptr<ImageData>& i){return at?read<RECT>(p,at):RECT{0,0,LONG(i->width),LONG(i->height)};}
    static bool valid(const RECT& r,U32 w,U32 h){return r.left>=0 && r.top>=0 && r.right>r.left && r.bottom>r.top && U32(r.right)<=w && U32(r.bottom)<=h;}
    U32 lock_rect(Object& o,U32 level,U32 at,U32 area,U32 flags){p.memory.check(at,8,Memory::Write);if(o.locks.contains(level))return U32(D3DERR_INVALIDCALL);auto i=image(o.resource,level);const auto r=rectangle(area,i);if(!valid(r,i->width,i->height) || !bpp(i->format))return U32(D3DERR_INVALIDCALL);download(i);const auto row=U32(r.right-r.left)*bpp(i->format),pitch=(row+3u)&~3u,rows=U32(r.bottom-r.top);auto guest=p.allocate_bytes(std::size_t(pitch)*rows);if(!(flags&D3DLOCK_DISCARD))for(U32 y=0;y<rows;++y)p.memory.copy_in(guest+y*pitch,std::span(i->bytes.data()+(std::size_t(y+r.top)*i->width+r.left)*bpp(i->format),row));const bool ro=(flags&D3DLOCK_READONLY)!=0;if(ro)p.memory.protect(guest,(std::size_t(pitch)*rows+4095)&~std::size_t(4095),Memory::Read);o.locks.emplace(level,Lock{guest,pitch,rows,row,0,r,ro});write(p,at,std::array<U32,2>{pitch,guest});return 0;}
    U32 unlock_rect(Object& o,U32 level){auto it=o.locks.find(level);if(it==o.locks.end())return U32(D3DERR_INVALIDCALL);const auto l=it->second;auto i=image(o.resource,level);if(!l.readonly){auto bytes=array<std::uint8_t>(l.guest,std::size_t(l.pitch)*l.rows);for(U32 y=0;y<l.rows;++y)std::memcpy(i->bytes.data()+(std::size_t(y+l.area.top)*i->width+l.area.left)*bpp(i->format),bytes.data()+y*l.pitch,l.row_bytes);i->cpu_dirty=true;i->gpu_dirty=false;++uploads;}p.memory.release(l.guest);o.locks.erase(it);return 0;}
    U32 lock_buffer(Object& o,Args a){p.memory.check(a[3],4,Memory::Write);if(!o.locks.empty())return U32(D3DERR_INVALIDCALL);auto& bytes=o.resource->bytes;if(a[1]>=bytes.size() || (a[2] && a[2]>bytes.size()-a[1]))return U32(D3DERR_INVALIDCALL);U32 n=a[2]?a[2]:U32(bytes.size())-a[1];const auto guest=p.allocate_bytes(n);if(!(a[4]&D3DLOCK_DISCARD))p.memory.copy_in(guest,std::span(bytes.data()+a[1],n));const bool ro=(a[4]&D3DLOCK_READONLY)!=0;if(ro)p.memory.protect(guest,(std::size_t(n)+4095)&~std::size_t(4095),Memory::Read);o.locks.emplace(0,Lock{guest,n,1,n,a[1],{},ro});p.memory.store(a[3],guest,32);return 0;}
    U32 unlock_buffer(Object& o){auto it=o.locks.find(0);if(it==o.locks.end())return U32(D3DERR_INVALIDCALL);const auto l=it->second;if(!l.readonly){p.memory.copy_out(l.guest,std::span(o.resource->bytes.data()+l.offset,l.row_bytes));++uploads;}p.memory.release(l.guest);o.locks.erase(it);return 0;}
    U32 method(Object&,unsigned,Args);U32 device(unsigned,Args);void init(U32,U32);void defaults();
    void bind_same_states();void refresh_same_states();void update_default_viewport();void sync_native_textures();
    void set_texture(unsigned unit,Res texture,U32 token){
        if(state.textures[unit]!=texture){texture_differences-=state.textures[unit]!=baseline_textures[unit];texture_differences+=texture!=baseline_textures[unit];++texture_version;}
        state.textures[unit]=std::move(texture);same_textures[unit]=committed_textures[unit]=token;
    }
    void reset_baseline(){baseline_words={state.render,state.stage,state.sampler};baseline_textures=state.textures;word_differences=texture_differences=0;}
    void recount_differences(){word_differences=texture_differences=0;for(unsigned j=0;j<256;++j)word_differences+=state.render[j]!=baseline_words.render[j];
        for(unsigned unit=0;unit<8;++unit)for(unsigned j=0;j<33;++j)word_differences+=state.stage[unit][j]!=baseline_words.stage[unit][j];
        for(unsigned unit=0;unit<16;++unit){for(unsigned j=0;j<14;++j)word_differences+=state.sampler[unit][j]!=baseline_words.sampler[unit][j];texture_differences+=state.textures[unit]!=baseline_textures[unit];}}
    void set_word(U32& destination,U32 value,U32 baseline){if(destination!=value){word_differences-=destination!=baseline;word_differences+=value!=baseline;destination=value;}}
    void apply();void flush() noexcept;void bind_sampler(unsigned,const Res&);U32 draw(U32,U32,std::span<const std::uint8_t>,U32,std::span<const U32> indices = {});
    U32 present(Args);void capture(bool=false);D3DCAPS9 caps();
    U32 copy(const std::shared_ptr<ImageData>&,RECT,const std::uint8_t*,U32,D3DFORMAT,RECT,U32,U32);
    std::vector<DWORD> shader(U32);U32 compile(U32,U32,Kind);
public:
    explicit Graphics(Process& q):p(q),state_apis(wa2_state_apis(q.memory,&q.api_calls)){}~Graphics(){shutdown();}
    void install();void shutdown() noexcept;
    std::string batch_report() const{return "{\"submitted_draws\":"+std::to_string(submitted_draws)+",\"batched_guest_draws\":"+std::to_string(batched_guest_draws)+",\"submitted_vertices\":"+std::to_string(submitted_vertices)+",\"pending_vertices\":"+std::to_string(pending_vertices.size())+",\"native_texture_changes\":"+std::to_string(native_texture_version)+",\"native_texture_syncs\":"+std::to_string(native_texture_syncs)+"}";}
    std::string lifetime_report() const{return "{\"live_objects\":"+std::to_string(objects.size())+",\"resource_entries\":"+std::to_string(resources.size())+",\"texture_entries\":"+std::to_string(textures_alive.size())+",\"image_entries\":"+std::to_string(images.size())+"}";}
    std::string sampler_report() const{return "{\"enabled\":"+std::string(sampler_cache_enabled?"true":"false")+",\"parameter_calls\":"+std::to_string(sampler_parameter_calls)+",\"parameter_skips\":"+std::to_string(sampler_parameter_skips)+",\"frame_error_checks\":"+std::to_string(frame_error_checks)+",\"requested_swap_interval\":"+std::to_string(requested_swap_interval)+",\"actual_swap_interval\":"+std::to_string(actual_swap_interval)+",\"swap_interval_result\":"+std::to_string(swap_interval_result)+"}";}
    std::string report() const {LARGE_INTEGER reported{};::QueryPerformanceCounter(&reported);std::ostringstream o;o<<"{\"backend\":\"sdl2-opengl\",\"objects_created\":"<<created<<",\"devices_created\":"<<devices<<",\"textures_created\":"<<textures<<",\"presents\":"<<presents<<",\"draw_calls\":"<<draws<<",\"uploads\":"<<uploads<<",\"frames_captured\":"<<captures<<",\"capture_hresult\":"<<U32(capture_result)<<",\"backbuffer_width\":"<<presentation[0]<<",\"backbuffer_height\":"<<presentation[1]<<",\"windowed\":"<<presentation[8]<<",\"last_blit\":{\"source_width\":"<<last_source_width<<",\"source_height\":"<<last_source_height<<",\"destination_width\":"<<last_destination_width<<",\"destination_height\":"<<last_destination_height<<",\"source_rect\":["<<last_blit_source.left<<','<<last_blit_source.top<<','<<last_blit_source.right<<','<<last_blit_source.bottom<<"],\"destination_rect\":["<<last_blit_destination.left<<','<<last_blit_destination.top<<','<<last_blit_destination.right<<','<<last_blit_destination.bottom<<"]},\"calls\":{";bool first=true;for(const auto& [k,v]:calls){if(!first)o<<',';first=false;o<<'"'<<k<<"\":"<<v;}o<<"},\"frame_times_ms\":{";first=true;for(const auto& [frame,ms]:frame_times_ms){if(!first)o<<',';first=false;o<<'"'<<frame<<"\":"<<ms;}o<<"},\"source_bytes_before_crop\":"<<source_bytes_before_crop<<",\"source_bytes_staged\":"<<source_bytes_staged<<",\"surface_load_profile\":[";first=true;for(const auto& [key,value]:copy_samples){if(!first)o<<',';first=false;o<<"{\"conversion\":\""<<key<<"\",\"calls\":"<<value.calls<<",\"pixels\":"<<value.pixels<<",\"ms\":"<<double(value.total_ns)/1e6<<",\"readback_ms\":"<<double(value.readback_ns)/1e6<<'}';}o<<"],\"presentation_clock_hz\":"<<clock_frequency<<",\"recent_presentations\":[";first=true;for(const auto& frame:recent_frames){if(!first)o<<char(44);first=false;o<<char(91)<<frame.frame<<char(44)<<frame.counter<<char(44)<<frame.foreground<<char(44)<<frame.minimized<<char(93);}o<<"],\"state_applies\":"<<state_applies<<",\"state_cache_hits\":"<<state_cache_hits<<",\"framebuffer_checks\":"<<framebuffer_checks<<",\"texture_only_applies\":"<<texture_only_applies<<",\"reported_counter\":"<<reported.QuadPart<<",\"driver\":{\"vendor\":"<<std::quoted(driver_vendor)<<",\"renderer\":"<<std::quoted(driver_renderer)<<",\"version\":"<<std::quoted(driver_version)<<"}}";return o.str();}
};
void Graphics::defaults(){
    applied.reset();
    ++config_version;++texture_version;
    const auto back=image(backbuffer);state=State{};state.target=backbuffer;
    state.viewport={0,0,back->width,back->height,0,1};state.scissor={0,0,LONG(back->width),LONG(back->height)};
    auto& r=state.render;r[D3DRS_ZENABLE]=1;r[D3DRS_ZWRITEENABLE]=1;r[D3DRS_ZFUNC]=D3DCMP_LESSEQUAL;
    r[D3DRS_FILLMODE]=D3DFILL_SOLID;r[D3DRS_SHADEMODE]=D3DSHADE_GOURAUD;r[D3DRS_CULLMODE]=D3DCULL_CCW;
    r[D3DRS_SRCBLEND]=D3DBLEND_ONE;r[D3DRS_DESTBLEND]=D3DBLEND_ZERO;r[D3DRS_BLENDOP]=D3DBLENDOP_ADD;
    r[D3DRS_SRCBLENDALPHA]=D3DBLEND_ONE;r[D3DRS_DESTBLENDALPHA]=D3DBLEND_ZERO;r[D3DRS_BLENDOPALPHA]=D3DBLENDOP_ADD;
    r[D3DRS_ALPHAFUNC]=D3DCMP_ALWAYS;r[D3DRS_TEXTUREFACTOR]=0xffffffff;r[D3DRS_COLORWRITEENABLE]=15;r[D3DRS_BLENDFACTOR]=0xffffffff;
    r[D3DRS_STENCILFUNC]=D3DCMP_ALWAYS;r[D3DRS_STENCILMASK]=0xffffffff;r[D3DRS_STENCILWRITEMASK]=0xffffffff;
    for(unsigned n=0;n<8;++n){auto& t=state.stage[n];t[D3DTSS_COLOROP]=n?D3DTOP_DISABLE:D3DTOP_MODULATE;t[D3DTSS_COLORARG1]=D3DTA_TEXTURE;t[D3DTSS_COLORARG2]=D3DTA_CURRENT;t[D3DTSS_ALPHAOP]=n?D3DTOP_DISABLE:D3DTOP_SELECTARG1;t[D3DTSS_ALPHAARG1]=D3DTA_TEXTURE;t[D3DTSS_ALPHAARG2]=D3DTA_CURRENT;t[D3DTSS_TEXCOORDINDEX]=n;}
    for(auto& s:state.sampler){s[D3DSAMP_ADDRESSU]=s[D3DSAMP_ADDRESSV]=s[D3DSAMP_ADDRESSW]=D3DTADDRESS_WRAP;s[D3DSAMP_MAGFILTER]=s[D3DSAMP_MINFILTER]=D3DTEXF_POINT;}
    D3DMATRIX identity{};for(unsigned n=0;n<4;++n)identity.m[n][n]=1;for(auto t:{U32(D3DTS_WORLD),U32(D3DTS_VIEW),U32(D3DTS_PROJECTION)})state.transforms[t]=identity;for(unsigned n=0;n<8;++n)state.transforms[D3DTS_TEXTURE0+n]=identity;
    reset_baseline();
}
void Graphics::update_default_viewport(){const auto target=image(state.target);const D3DVIEWPORT9 full{0,0,target->width,target->height,0,1};same_target[1]=std::memcmp(&state.viewport,&full,sizeof(full))==0;}
void Graphics::bind_same_states(){
    if(!state_apis || !device_address)return;
    auto bind=[&](unsigned slot,Wa2StateApiKind kind,U32* values,U32 columns){auto& count=calls["2."+std::string(methods_device[slot].name)];
        wa2_bind_state_api(*state_apis,p.memory.load(device_table+slot*4,32),device_address,kind,values,1,columns,&count);};
    if(state.declaration)wa2_unbind_state_api(*state_apis,p.memory.load(device_table+89*4,32));else bind(89,Wa2StateApiKind::fvf_same,&state.fvf,1);
    bind(37,Wa2StateApiKind::target_same,same_target.data(),2);
    auto& texture_calls=calls["2.SetTexture"];
    wa2_bind_state_api(*state_apis,p.memory.load(device_table+65*4,32),device_address,Wa2StateApiKind::texture_binding,
        same_textures.data(),U32(live_texture_tokens.size()),U32(same_textures.size()),&texture_calls,&native_texture_version,live_texture_tokens.data());
}
void Graphics::sync_native_textures(){
    if(native_texture_version==committed_texture_version)return;
    for(unsigned unit=0;unit<same_textures.size();++unit){const auto token=same_textures[unit];
        if(token!=committed_textures[unit] && token!=0xffffffffu)set_texture(unit,get(token,Kind::texture,true),token);
    }
    committed_texture_version=native_texture_version;++native_texture_syncs;
}
void Graphics::refresh_same_states(){
    same_target[0]=0xffffffffu;update_default_viewport();
    for(const auto& [at,object]:objects)if(object.refs && object.resource==state.target){same_target[0]=at;break;}
    for(unsigned unit=0;unit<same_textures.size();++unit){same_textures[unit]=state.textures[unit]?0xffffffffu:0;
        if(state.textures[unit])for(const auto& [at,object]:objects)if(object.refs && object.resource==state.textures[unit]){same_textures[unit]=at;break;}}
    committed_textures=same_textures;committed_texture_version=native_texture_version;
    bind_same_states();
}
void Graphics::init(U32 pp,U32 focus){
    if(context)stop("multiple devices are outside this target profile");presentation=read<std::array<U32,14>>(p,pp);focus_window=focus;
    const auto guest_window=presentation[7]?presentation[7]:focus;auto hwnd=reinterpret_cast<HWND>(p.gui()->native_window(guest_window));if(!hwnd)stop("device requires a window");
    host_window=hwnd;host_procedure=reinterpret_cast<WNDPROC>(::GetWindowLongPtrA(hwnd,GWLP_WNDPROC));if(!host_procedure)stop("device window has no native procedure");
    SDL_SetHint(SDL_HINT_VIDEO_FOREIGN_WINDOW_OPENGL,"1");SDL_SetHint(SDL_HINT_WINDOWS_ENABLE_MESSAGELOOP,"0");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);SDL_GL_SetAttribute(SDL_GL_RED_SIZE,8);SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,8);SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,8);
    if(SDL_GL_LoadLibrary(nullptr))stop(SDL_GetError());window=SDL_CreateWindowFrom(hwnd);if(!window)stop(SDL_GetError());hook_layout();context=SDL_GL_CreateContext(window);if(!context)stop(SDL_GetError());
    gl.init();glGetIntegerv(GL_MAX_TEXTURE_SIZE,&max_texture);mojo=MOJOSHADER_glCreateContext(MOJOSHADER_PROFILE_GLSL120,[](const char* name,void*)->void*{return SDL_GL_GetProcAddress(name);},nullptr,nullptr,nullptr,nullptr);if(!mojo)stop(MOJOSHADER_glGetError());MOJOSHADER_glMakeContextCurrent(mojo);
    const auto driver_string=[](GLenum name){const auto* value=glGetString(name);return value?std::string(reinterpret_cast<const char*>(value)):std::string{};};
    driver_vendor=driver_string(GL_VENDOR);driver_renderer=driver_string(GL_RENDERER);driver_version=driver_string(GL_VERSION);
    requested_swap_interval=presentation[13]==D3DPRESENT_INTERVAL_IMMEDIATE?0:1;
    swap_interval_result=SDL_GL_SetSwapInterval(requested_swap_interval);actual_swap_interval=SDL_GL_GetSwapInterval();
    RECT client{};::GetClientRect(hwnd,&client);if(!presentation[0])presentation[0]=std::max(1L,client.right);if(!presentation[1])presentation[1]=std::max(1L,client.bottom);if(!presentation[2])presentation[2]=D3DFMT_X8R8G8B8;
    if(!presentation[8] && SDL_SetWindowFullscreen(window,SDL_WINDOW_FULLSCREEN_DESKTOP))stop(SDL_GetError());
    device_resource=make(Kind::device);backbuffer=surface(new_image(presentation[0],presentation[1],D3DFORMAT(presentation[2]),D3DUSAGE_RENDERTARGET,D3DPOOL_DEFAULT));defaults();
    if(presentation[9])state.depth=surface(new_image(presentation[0],presentation[1],D3DFORMAT(presentation[10]),D3DUSAGE_DEPTHSTENCIL,D3DPOOL_DEFAULT));
    write(p,pp,presentation);++devices;
}
D3DCAPS9 Graphics::caps(){
    D3DCAPS9 c{};c.DeviceType=D3DDEVTYPE_HAL;c.Caps=D3DCAPS_READ_SCANLINE;c.PresentationIntervals=D3DPRESENT_INTERVAL_ONE|D3DPRESENT_INTERVAL_IMMEDIATE;
    c.DevCaps=D3DDEVCAPS_DRAWPRIMTLVERTEX|D3DDEVCAPS_EXECUTESYSTEMMEMORY|D3DDEVCAPS_TEXTURESYSTEMMEMORY|D3DDEVCAPS_TEXTUREVIDEOMEMORY|D3DDEVCAPS_DRAWPRIMITIVES2|D3DDEVCAPS_DRAWPRIMITIVES2EX|D3DDEVCAPS_HWRASTERIZATION;
    c.PrimitiveMiscCaps=D3DPMISCCAPS_CULLNONE|D3DPMISCCAPS_CULLCW|D3DPMISCCAPS_CULLCCW|D3DPMISCCAPS_COLORWRITEENABLE|D3DPMISCCAPS_BLENDOP|D3DPMISCCAPS_SEPARATEALPHABLEND;
    c.RasterCaps=D3DPRASTERCAPS_SCISSORTEST;c.ZCmpCaps=c.AlphaCmpCaps=255;c.SrcBlendCaps=c.DestBlendCaps=0x3fff;
    c.ShadeCaps=D3DPSHADECAPS_COLORGOURAUDRGB|D3DPSHADECAPS_ALPHAGOURAUDBLEND;
    c.TextureCaps=D3DPTEXTURECAPS_ALPHA|D3DPTEXTURECAPS_PERSPECTIVE;c.TextureFilterCaps=D3DPTFILTERCAPS_MINFPOINT|D3DPTFILTERCAPS_MINFLINEAR|D3DPTFILTERCAPS_MAGFPOINT|D3DPTFILTERCAPS_MAGFLINEAR;
    c.TextureAddressCaps=D3DPTADDRESSCAPS_WRAP|D3DPTADDRESSCAPS_MIRROR|D3DPTADDRESSCAPS_CLAMP|D3DPTADDRESSCAPS_BORDER|D3DPTADDRESSCAPS_INDEPENDENTUV;
    c.MaxTextureWidth=c.MaxTextureHeight=max_texture;c.MaxTextureRepeat=8192;c.MaxTextureAspectRatio=max_texture;c.MaxAnisotropy=1;c.MaxVertexW=1e10f;
    c.MaxTextureBlendStages=c.MaxSimultaneousTextures=8;c.TextureOpCaps=D3DTEXOPCAPS_DISABLE|D3DTEXOPCAPS_SELECTARG1|D3DTEXOPCAPS_SELECTARG2|D3DTEXOPCAPS_MODULATE|D3DTEXOPCAPS_MODULATE2X|D3DTEXOPCAPS_MODULATE4X|D3DTEXOPCAPS_ADD|D3DTEXOPCAPS_SUBTRACT|D3DTEXOPCAPS_BLENDTEXTUREALPHA;
    c.MaxPrimitiveCount=1048575;c.MaxVertexIndex=1048575;c.MaxStreams=1;c.MaxStreamStride=256;c.VertexShaderVersion=D3DVS_VERSION(2,0);c.MaxVertexShaderConst=256;c.PixelShaderVersion=D3DPS_VERSION(2,0);c.PixelShader1xMaxValue=8;
    c.VS20Caps.NumTemps=32;c.VS20Caps.StaticFlowControlDepth=4;c.PS20Caps.NumTemps=32;c.PS20Caps.NumInstructionSlots=512;c.PS20Caps.StaticFlowControlDepth=4;c.NumSimultaneousRTs=1;return c;
}
U32 Graphics::copy(const std::shared_ptr<ImageData>& d,RECT dr,const std::uint8_t* source,U32 pitch,D3DFORMAT f,RECT sr,U32 filter,U32 key){
    if(!valid(dr,d->width,d->height) || sr.left<0 || sr.top<0 || sr.right<=sr.left || sr.bottom<=sr.top || !bpp(f) || !bpp(d->format) || std::uint64_t(sr.right)*bpp(f)>pitch)return U32(D3DERR_INVALIDCALL);
    const auto sw=sr.right-sr.left,sh=sr.bottom-sr.top,dw=dr.right-dr.left,dh=dr.bottom-dr.top;
    CopySample* sample{};
    if(p.options.profile_execution){const auto key=std::to_string(U32(f))+":"+std::to_string(U32(d->format))+":"+std::to_string(sw)+"x"+std::to_string(sh)+":"+std::to_string(dw)+"x"+std::to_string(dh);sample=&copy_samples[key];++sample->calls;sample->pixels+=std::uint64_t(dw)*dh;}
    ++copy_total.calls;copy_total.pixels+=std::uint64_t(dw)*dh;CopyTimer timing(sample,&copy_total);
    const auto before=std::chrono::steady_clock::now();download(d);
    const auto readback_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-before).count();
    copy_total.readback_ns+=readback_ns;if(sample)sample->readback_ns+=readback_ns;
    if(sw==dw && sh==dh && f==d->format && !key && f!=D3DFMT_X8R8G8B8 && f!=D3DFMT_X8B8G8R8 && f!=D3DFMT_X1R5G5B5){
        const auto size=bpp(f);const auto row=std::size_t(dw)*size;
        const auto source_begin=reinterpret_cast<std::uintptr_t>(source),destination_begin=reinterpret_cast<std::uintptr_t>(d->bytes.data());
        const bool alias=source_begin<destination_begin+d->bytes.size() && destination_begin<source_begin+std::size_t(sr.bottom)*pitch;
        if(!alias){for(LONG y=0;y<dh;++y)std::memcpy(d->bytes.data()+(std::size_t(y+dr.top)*d->width+dr.left)*size,source+std::size_t(y+sr.top)*pitch+std::size_t(sr.left)*size,row);d->cpu_dirty=true;d->gpu_dirty=false;++uploads;return 0;}
    }
    const bool src32=f==D3DFMT_A8R8G8B8 || f==D3DFMT_X8R8G8B8 || f==D3DFMT_A8B8G8R8 || f==D3DFMT_X8B8G8R8;
    const bool dst32=d->format==D3DFMT_A8R8G8B8 || d->format==D3DFMT_X8R8G8B8 || d->format==D3DFMT_A8B8G8R8 || d->format==D3DFMT_X8B8G8R8;
    if(sw==dw && sh==dh && src32 && dst32){
        const bool source_bgra=f==D3DFMT_A8R8G8B8 || f==D3DFMT_X8R8G8B8,destination_bgra=d->format==D3DFMT_A8R8G8B8 || d->format==D3DFMT_X8R8G8B8;
        const bool source_alpha=f==D3DFMT_A8R8G8B8 || f==D3DFMT_A8B8G8R8,destination_alpha=d->format==D3DFMT_A8R8G8B8 || d->format==D3DFMT_A8B8G8R8;
        for(LONG y=0;y<dh;++y){const auto* input=source+std::size_t(y+sr.top)*pitch+std::size_t(sr.left)*4;auto* output=d->bytes.data()+(std::size_t(y+dr.top)*d->width+dr.left)*4;
            for(LONG x=0;x<dw;++x){const auto* pixel=input+x*4;auto* target=output+x*4;auto red=pixel[source_bgra?2:0],green=pixel[1],blue=pixel[source_bgra?0:2];auto alpha=source_alpha?pixel[3]:std::uint8_t(255);if(key && ((U32(alpha)<<24|U32(red)<<16|U32(green)<<8|blue)==key))red=green=blue=alpha=0;target[destination_bgra?2:0]=red;target[1]=green;target[destination_bgra?0:2]=blue;target[3]=destination_alpha?alpha:255;}}
        d->cpu_dirty=true;d->gpu_dirty=false;++uploads;return 0;
    }
    const bool unscaled=(filter&15)==1;
    const bool linear=(filter&15)>=3 && (sw!=dw || sh!=dh);
    const auto point_x=(std::uint64_t(sw)<<16)/dw,point_y=(std::uint64_t(sh)<<16)/dh;
    for(LONG y=0;y<dh;++y)for(LONG x=0;x<dw;++x){
        auto sample=[&](LONG sx,LONG sy){return decode(source+std::size_t(std::clamp(sy,sr.top,sr.bottom-1))*pitch+std::size_t(std::clamp(sx,sr.left,sr.right-1))*bpp(f),f);};
        std::array<std::uint8_t,4> c{};
        if(unscaled){if(x<sw && y<sh)c=sample(sr.left+x,sr.top+y);}
        else c=sample(sr.left+LONG((std::uint64_t(x)*point_x)>>16),sr.top+LONG((std::uint64_t(y)*point_y)>>16));
        if(linear){const float sx=(float(x)+.5f)*sw/dw-.5f,sy=(float(y)+.5f)*sh/dh-.5f;const auto ix=LONG(std::floor(sx)),iy=LONG(std::floor(sy));const float fx=sx-ix,fy=sy-iy;const auto a=sample(sr.left+ix,sr.top+iy),b=sample(sr.left+ix+1,sr.top+iy),e=sample(sr.left+ix,sr.top+iy+1),f1=sample(sr.left+ix+1,sr.top+iy+1);for(unsigned n=0;n<4;++n)c[n]=std::uint8_t(std::clamp(std::lround((a[n]*(1-fx)+b[n]*fx)*(1-fy)+(e[n]*(1-fx)+f1[n]*fx)*fy),0L,255L));}
        if(key && ((U32(c[3])<<24|U32(c[0])<<16|U32(c[1])<<8|c[2])==key))c={0,0,0,0};
        encode(d->bytes.data()+(std::size_t(y+dr.top)*d->width+x+dr.left)*bpp(d->format),d->format,c);
    }
    d->cpu_dirty=true;d->gpu_dirty=false;++uploads;return 0;
}
std::vector<DWORD> Graphics::shader(U32 at){
    for(const auto& [addr,o]:objects)if(o.resource->kind==Kind::buffer && o.resource->data==at)return array<DWORD>(at,o.resource->bytes.size()/4);
    std::vector<DWORD> v{p.memory.load(at,32)};const auto major=(v[0]>>8)&255u;if(major<2 || major>3)stop("raw shader requires model 2/3");
    for(U32 offset=1;offset<16384;){auto token=p.memory.load(at+offset*4,32);v.push_back(token);++offset;if(token==0xffff)return v;const auto n=(token&0xffff)==0xfffe?(token>>16)&0x7fff:(token>>24)&15;if(n>16384-offset)stop("shader length limit");for(U32 j=0;j<n;++j)v.push_back(p.memory.load(at+(offset+j)*4,32));offset+=n;}stop("unterminated shader");
}
U32 Graphics::compile(U32 input,U32 output,Kind kind){
    applied.reset();
    p.memory.check(output,4,Memory::Write);p.memory.store(output,0,32);auto v=shader(input);auto* sh=MOJOSHADER_glCompileShader(reinterpret_cast<const unsigned char*>(v.data()),unsigned(v.size()*4),nullptr,0,nullptr,0);if(!sh)stop(std::string("shader compilation: ")+MOJOSHADER_glGetError());auto r=make(kind);r->shader=sh;r->cleanup=[sh]{MOJOSHADER_glDeleteShader(sh);};return out_res(output,r);
}
void Graphics::shutdown() noexcept {
    if(stopped)return;stopped=true;
    if(context){SDL_GL_MakeCurrent(window,context);MOJOSHADER_glMakeContextCurrent(mojo);flush();if(mojo)MOJOSHADER_glBindShaders(nullptr,nullptr);}
    applied.reset();
    state_apis.reset();
    for(auto& [at,o]:objects){for(const auto& [level,l]:o.locks)try{p.memory.release(l.guest);}catch(...){}if(o.resource->data)try{p.memory.release(o.resource->data);o.resource->data=0;}catch(...){}}
    for(auto& [key,program]:programs)MOJOSHADER_glDeleteProgram(program);programs.clear();
    for(auto& w:resources)if(auto r=w.lock())if(r->cleanup){r->cleanup();r->cleanup={};r->shader=nullptr;}
    state=State{};recording.reset();objects.clear();device_resource.reset();backbuffer.reset();root.reset();
    for(auto& w:images)if(auto i=w.lock())if(i->cleanup){i->cleanup();i->cleanup={};}
    for(auto& w:textures_alive)if(auto t=w.lock())if(t->cleanup){t->cleanup();t->cleanup={};}
    if(mojo){MOJOSHADER_glDestroyContext(mojo);mojo=nullptr;}if(context){SDL_GL_DeleteContext(context);context=nullptr;}unhook_layout();if(window){SDL_DestroyWindow(window);window=nullptr;}if(initialized){SDL_QuitSubSystem(SDL_INIT_VIDEO);initialized=false;}
}
U32 Graphics::method(Object& o,unsigned slot,Args a){
    const auto r=o.resource;
    // Setters below alter only the CPU state. Leave earlier geometry queued
    // until the next draw chooses a pipeline, or an observable resource/API
    // boundary requires it. Resource operations and EndScene always flush.
    const bool deferred=r->kind==Kind::device && ((slot>=81 && slot<=84) ||
        slot==37 || slot==39 || slot==44 || slot==46 || slot==47 || slot==57 ||
        slot==65 || slot==67 || slot==69 || slot==75 || slot==87 || slot==89 ||
        slot==92 || slot==100 || slot==102 || slot==104 || slot==107 ||
        (slot>=94 && slot<=99) || (slot>=109 && slot<=114));
    if(!deferred)flush();
    if(slot==0){const auto iid=read<GUID>(p,a[1]);p.memory.check(a[2],4,Memory::Write);p.memory.store(a[2],0,32);bool matches=IsEqualIID(iid,IID_IUnknown);
#define QI(type,k) matches=matches || (r->kind==Kind::k && IsEqualIID(iid,IID_##type))
        QI(IDirect3D9,d3d);QI(IDirect3DDevice9,device);QI(IDirect3DTexture9,texture);QI(IDirect3DSurface9,surface);QI(IDirect3DVertexBuffer9,vertexbuffer);QI(IDirect3DIndexBuffer9,indexbuffer);QI(IDirect3DVertexShader9,vertexshader);QI(IDirect3DPixelShader9,pixelshader);QI(IDirect3DVertexDeclaration9,declaration);QI(IDirect3DStateBlock9,stateblock);
#undef QI
        if(r->kind==Kind::texture)matches=matches || IsEqualIID(iid,IID_IDirect3DBaseTexture9);
        if(r->kind==Kind::texture || r->kind==Kind::surface || r->kind==Kind::vertexbuffer || r->kind==Kind::indexbuffer)matches=matches || IsEqualIID(iid,IID_IDirect3DResource9);
        static constexpr GUID buffer_iid{0x8ba5fb08,0x5195,0x40e2,{0xac,0x58,0x0d,0x98,0x9c,0x3a,0x01,0x02}};if(r->kind==Kind::buffer)matches=matches || IsEqualIID(iid,buffer_iid);
        return matches?out_res(a[2],r):U32(E_NOINTERFACE);
    }
    if(slot==1)return ++o.refs;
    if(slot==2){if(o.refs==1 && !o.locks.empty())stop("final release of locked resource");const auto refs=--o.refs;if(!refs){if(state_apis && r->kind==Kind::device){wa2_unbind_state_receiver(*state_apis,a[0]);device_address=0;}if(r->kind==Kind::texture)live_texture_tokens[a[0]>>16]=0;if(same_target[0]==a[0])same_target[0]=0xffffffffu;for(unsigned unit=0;unit<same_textures.size();++unit)if(same_textures[unit]==a[0])same_textures[unit]=committed_textures[unit]=0xffffffffu;if(r->data){p.memory.release(r->data);r->data=0;}p.memory.release(a[0]);objects.erase(a[0]);}return refs;}
    if(r->kind==Kind::d3d){const auto fmt=[](U32 f){return bpp(D3DFORMAT(f)) || depth_format(D3DFORMAT(f));};switch(slot){
        case 4:return U32(std::max(0,SDL_GetNumVideoDisplays()));
        case 5:{D3DADAPTER_IDENTIFIER9 v{};std::strcpy(v.Driver,"SDL2/OpenGL");std::strcpy(v.Description,"SDL2 OpenGL graphics adapter");std::strcpy(v.DeviceName,"SDL2 display");p.memory.check(a[3],1100,Memory::Write);p.memory.copy_in(a[3],std::span(reinterpret_cast<const std::uint8_t*>(&v),1100));return 0;}
        case 6:return a[1]<U32(SDL_GetNumVideoDisplays()) && fmt(a[2])?U32(std::max(0,SDL_GetNumDisplayModes(int(a[1])))):0;
        case 7:case 8:{SDL_DisplayMode m{};if((slot==7?SDL_GetDisplayMode(int(a[1]),int(a[3]),&m):SDL_GetCurrentDisplayMode(int(a[1]),&m)))return U32(D3DERR_INVALIDCALL);return output(slot==7?a[4]:a[2],D3DDISPLAYMODE{U32(m.w),U32(m.h),U32(m.refresh_rate),slot==7?D3DFORMAT(a[2]):D3DFMT_X8R8G8B8});}
        case 9:return fmt(a[3]) && fmt(a[4])?0:U32(D3DERR_NOTAVAILABLE);
        case 10:return fmt(a[6])?0:U32(D3DERR_NOTAVAILABLE);
        case 11:if(a[6])p.memory.store(a[6],a[5]==D3DMULTISAMPLE_NONE?1:0,32);return a[5]==D3DMULTISAMPLE_NONE?0:U32(D3DERR_NOTAVAILABLE);
        case 12:return depth_format(D3DFORMAT(a[5]))?0:U32(D3DERR_NOTAVAILABLE);
        case 13:return fmt(a[3]) && fmt(a[4])?0:U32(D3DERR_NOTAVAILABLE);
        case 14:return output(a[3],caps());case 15:return a[1]<U32(SDL_GetNumVideoDisplays())?0xbc000000+a[1]*16:0;
        case 16:p.memory.check(a[6],4,Memory::Write);p.memory.store(a[6],0,32);p.memory.check(a[5],56,Memory::Read|Memory::Write);creation_flags=a[4];init(a[5],a[3]);if(!(a[4]&D3DCREATE_FPU_PRESERVE))p.cpu.fp.put16(0,std::uint16_t((p.cpu.fp.get16(0)&~0x0f00u)|0x003fu));return out_res(a[6],device_resource);
    }}
    if(r->kind==Kind::device)return device(slot,a);
    if(slot==3 && r->kind!=Kind::buffer)return out_res(a[1],r->device.lock());
    const bool resource=r->kind==Kind::texture || r->kind==Kind::surface || r->kind==Kind::vertexbuffer || r->kind==Kind::indexbuffer;
    if(resource)switch(slot){case 7:{const auto old=r->priority;r->priority=a[1];return old;}case 8:return r->priority;case 9:if(r->kind==Kind::texture)for(const auto& i:r->levels)upload(i);return 0;case 10:return r->kind==Kind::texture?D3DRTYPE_TEXTURE:r->kind==Kind::surface?D3DRTYPE_SURFACE:r->kind==Kind::vertexbuffer?D3DRTYPE_VERTEXBUFFER:D3DRTYPE_INDEXBUFFER;}
    if(r->kind==Kind::texture)switch(slot){case 11:{const auto old=r->lod;r->lod=std::min(a[1],U32(r->levels.size()-1));return old;}case 12:return r->lod;case 13:return U32(r->levels.size());case 17:if(a[1]>=r->levels.size())return U32(D3DERR_INVALIDCALL);return output(a[2],description(image(r,a[1])));case 18:if(a[1]>=r->levels.size())return U32(D3DERR_INVALIDCALL);return out_res(a[2],surface(image(r,a[1]),r));case 19:return lock_rect(o,a[1],a[2],a[3],a[4]);case 20:return unlock_rect(o,a[1]);case 21:if(a[1])read<RECT>(p,a[1]);return 0;}
    if(r->kind==Kind::surface)switch(slot){case 11:{read<GUID>(p,a[1]);auto parent=r->container.lock();if(!parent)return U32(E_NOINTERFACE);return out_res(a[2],parent);}case 12:return output(a[1],description(image(r)));case 13:return lock_rect(o,0,a[1],a[2],a[3]);case 14:return unlock_rect(o,0);}
    if(r->kind==Kind::vertexbuffer || r->kind==Kind::indexbuffer){if(slot==11)return lock_buffer(o,a);if(slot==12)return unlock_buffer(o);if(slot==13){if(r->kind==Kind::vertexbuffer){D3DVERTEXBUFFER_DESC d{};d.Type=D3DRTYPE_VERTEXBUFFER;d.Format=D3DFMT_VERTEXDATA;d.Usage=r->usage;d.Pool=D3DPOOL(r->pool);d.Size=U32(r->bytes.size());d.FVF=r->fvf;return output(a[1],d);}D3DINDEXBUFFER_DESC d{};d.Type=D3DRTYPE_INDEXBUFFER;d.Format=D3DFORMAT(r->format);d.Usage=r->usage;d.Pool=D3DPOOL(r->pool);d.Size=U32(r->bytes.size());return output(a[1],d);}}
    if(r->kind==Kind::stateblock){if(slot==4){r->saved=std::make_shared<State>(state);return 0;}if(slot==5){const auto target=state.target,depth=state.depth;const bool scene=state.scene;state=*r->saved;state.target=target;state.depth=depth;state.scene=scene;++config_version;++texture_version;recount_differences();refresh_same_states();return 0;}}
    if(r->kind==Kind::buffer){if(slot==4)return U32(r->bytes.size());if(slot==3){if(!r->data){r->data=p.allocate_bytes(r->bytes.size());p.memory.copy_in(r->data,r->bytes);}return r->data;}}
    stop(std::string("unimplemented ")+std::to_string(unsigned(r->kind))+"::"+methods(r->kind)[slot].name);
}
U32 Graphics::device(unsigned slot,Args a){switch(slot){
    case 3:return 0;case 4:return 256u*1024*1024;case 5:return 0;case 6:return out_res(a[1],root);case 7:return output(a[1],caps());
    case 8:{SDL_DisplayMode m{};if(SDL_GetCurrentDisplayMode(0,&m))return U32(D3DERR_INVALIDCALL);return output(a[2],D3DDISPLAYMODE{U32(m.w),U32(m.h),U32(m.refresh_rate),D3DFMT_X8R8G8B8});}
    case 9:return output(a[1],std::array<U32,4>{0,D3DDEVTYPE_HAL,focus_window,creation_flags});
    case 11:SDL_WarpMouseInWindow(window,int(std::bit_cast<std::int32_t>(a[1])),int(std::bit_cast<std::int32_t>(a[2])));return 0;
    case 12:return SDL_ShowCursor(a[1]?SDL_ENABLE:SDL_DISABLE)==SDL_ENABLE;case 15:return 1;
    case 17:return present(a);case 18:if(a[1] || a[2] || a[3]!=D3DBACKBUFFER_TYPE_MONO)return U32(D3DERR_INVALIDCALL);return out_res(a[4],backbuffer);
    case 19:return output(a[2],D3DRASTER_STATUS{FALSE,0});case 20:return 0;
    case 23:p.memory.check(a[7],4,Memory::Write);if(a[8])return U32(D3DERR_INVALIDCALL);return out_res(a[7],texture(a[1],a[2],a[3],a[4],D3DFORMAT(a[5]),a[6]));
    case 26:case 27:{p.memory.check(a[5],4,Memory::Write);if(!a[1] || a[1]>MaxBytes || a[6])return U32(D3DERR_INVALIDCALL);auto r=make(slot==26?Kind::vertexbuffer:Kind::indexbuffer);r->bytes.resize(a[1]);r->usage=a[2];r->format=slot==26?D3DFMT_VERTEXDATA:a[3];r->fvf=slot==26?a[3]:0;r->pool=a[4];return out_res(a[5],r);}
    case 28:case 29:{p.memory.check(a[7],4,Memory::Write);if(a[4] || a[5] || a[8])return U32(D3DERR_INVALIDCALL);return out_res(a[7],surface(new_image(a[1],a[2],D3DFORMAT(a[3]),slot==28?D3DUSAGE_RENDERTARGET:D3DUSAGE_DEPTHSTENCIL,D3DPOOL_DEFAULT)));}
    case 30:{auto src=image(get(a[1],Kind::surface)),dst=image(get(a[3],Kind::surface));auto sr=rectangle(a[2],src);POINT pt=a[4]?read<POINT>(p,a[4]):POINT{};if(!valid(sr,src->width,src->height))return U32(D3DERR_INVALIDCALL);download(src);return copy(dst,{pt.x,pt.y,pt.x+sr.right-sr.left,pt.y+sr.bottom-sr.top},src->bytes.data(),src->width*bpp(src->format),src->format,sr,1,0);}
    case 31:{auto src=get(a[1],Kind::texture),dst=get(a[2],Kind::texture);if(src->levels.size()!=dst->levels.size())return U32(D3DERR_INVALIDCALL);for(unsigned n=0;n<src->levels.size();++n){auto s=image(src,n),d=image(dst,n);download(s);const auto hr=copy(d,rectangle(0,d),s->bytes.data(),s->width*bpp(s->format),s->format,rectangle(0,s),1,0);if(hr)return hr;}return 0;}
    case 32:{auto src=image(get(a[1],Kind::surface)),dst=image(get(a[2],Kind::surface));if(src->width!=dst->width || src->height!=dst->height || src->format!=dst->format)return U32(D3DERR_INVALIDCALL);download(src);dst->bytes=src->bytes;dst->cpu_dirty=true;dst->gpu_dirty=false;return 0;}
    case 34:{auto src=image(get(a[1],Kind::surface)),dst=image(get(a[3],Kind::surface));auto sr=rectangle(a[2],src),dr=rectangle(a[4],dst);if(src==dst || !valid(sr,src->width,src->height) || !valid(dr,dst->width,dst->height) || depth_format(src->format) || depth_format(dst->format))return U32(D3DERR_INVALIDCALL);upload(src);upload(dst);fbo(src);fbo(dst);gl.BindFramebuffer(GL_READ_FRAMEBUFFER,src->fbo);gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,dst->fbo);glDisable(GL_SCISSOR_TEST);gl.BlitFramebuffer(sr.left,sr.top,sr.right,sr.bottom,dr.left,dr.top,dr.right,dr.bottom,GL_COLOR_BUFFER_BIT,a[5]==D3DTEXF_LINEAR?GL_LINEAR:GL_NEAREST);if(glGetError())stop("framebuffer blit failed");dst->gpu_dirty=true;dst->cpu_dirty=false;last_blit_source=sr;last_blit_destination=dr;last_source_width=src->width;last_source_height=src->height;last_destination_width=dst->width;last_destination_height=dst->height;return 0;}
    case 35:{auto dst=image(get(a[1],Kind::surface));auto r=rectangle(a[2],dst);if(!valid(r,dst->width,dst->height))return U32(D3DERR_INVALIDCALL);download(dst);const std::array<std::uint8_t,4> c{std::uint8_t(a[3]>>16),std::uint8_t(a[3]>>8),std::uint8_t(a[3]),std::uint8_t(a[3]>>24)};for(LONG y=r.top;y<r.bottom;++y)for(LONG x=r.left;x<r.right;++x)encode(dst->bytes.data()+(std::size_t(y)*dst->width+x)*bpp(dst->format),dst->format,c);dst->cpu_dirty=true;dst->gpu_dirty=false;return 0;}
    case 36:p.memory.check(a[5],4,Memory::Write);if(a[6])return U32(D3DERR_INVALIDCALL);return out_res(a[5],surface(new_image(a[1],a[2],D3DFORMAT(a[3]),0,a[4])));
    case 37:{if(a[1] || !a[2])return U32(D3DERR_INVALIDCALL);auto target=get(a[2],Kind::surface);const auto i=image(target);const D3DVIEWPORT9 viewport{0,0,i->width,i->height,0,1};if(state.target!=target || std::memcmp(&state.viewport,&viewport,sizeof(viewport)))++config_version;state.target=std::move(target);state.viewport=viewport;same_target={a[2],1};return 0;}
    case 38:return a[1]?U32(D3DERR_INVALIDCALL):out_res(a[2],state.target);
    case 39:{auto depth=get(a[1],Kind::surface,true);if(state.depth!=depth)++config_version;state.depth=std::move(depth);return 0;}case 40:return out_res(a[1],state.depth);
    case 41:if(state.scene)return U32(D3DERR_INVALIDCALL);state.scene=true;return 0;case 42:if(!state.scene)return U32(D3DERR_INVALIDCALL);state.scene=false;return 0;
    case 43:{auto i=image(state.target);upload(i);fbo(i);glColorMask(1,1,1,1);glDepthMask(1);glStencilMask(~0u);glDisable(GL_SCISSOR_TEST);const auto c=a[4];glClearColor(float((c>>16)&255)/255,float((c>>8)&255)/255,float(c&255)/255,float(c>>24)/255);glClearDepth(real(a[5]));glClearStencil(GLint(a[6]));GLbitfield bits=0;if(a[3]&D3DCLEAR_TARGET)bits|=GL_COLOR_BUFFER_BIT;if(a[3]&D3DCLEAR_ZBUFFER)bits|=GL_DEPTH_BUFFER_BIT;if(a[3]&D3DCLEAR_STENCIL)bits|=GL_STENCIL_BUFFER_BIT;
        if(a[1]){auto rects=array<D3DRECT>(a[2],a[1]);glEnable(GL_SCISSOR_TEST);for(const auto& r:rects){glScissor(r.x1,r.y1,r.x2-r.x1,r.y2-r.y1);glClear(bits);}glDisable(GL_SCISSOR_TEST);}else glClear(bits);i->gpu_dirty=true;return 0;}
    case 44:case 46:{auto m=read<D3DMATRIX>(p,a[2]);if(slot==46){const auto old=state.transforms[a[1]];D3DMATRIX out{};for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)for(unsigned n=0;n<4;++n)out.m[y][x]+=old.m[y][n]*m.m[n][x];m=out;}if(auto old=state.transforms.find(a[1]);old==state.transforms.end() || std::memcmp(&old->second,&m,sizeof(m)))++config_version;state.transforms[a[1]]=m;return 0;}
    case 45:return output(a[2],state.transforms[a[1]]);
    case 47:{auto v=read<D3DVIEWPORT9>(p,a[1]);auto i=image(state.target);if(!v.Width || !v.Height || std::uint64_t(v.X)+v.Width>i->width || std::uint64_t(v.Y)+v.Height>i->height)return U32(D3DERR_INVALIDCALL);if(std::memcmp(&state.viewport,&v,sizeof(v)))++config_version;state.viewport=v;update_default_viewport();return 0;}
    case 48:return output(a[1],state.viewport);
    case 57:if(a[1]>=state.render.size())return U32(D3DERR_INVALIDCALL);if(a[2] && (a[1]==D3DRS_LIGHTING || a[1]==D3DRS_FOGENABLE || a[1]==D3DRS_STENCILENABLE))stop("unmodeled enabled lighting/fog/stencil state "+std::to_string(a[1]));set_word(state.render[a[1]],a[2],baseline_words.render[a[1]]);return 0;
    case 58:if(a[1]>=state.render.size())return U32(D3DERR_INVALIDCALL);return output(a[2],state.render[a[1]]);
    case 59:{if(a[1]!=D3DSBT_ALL)return U32(D3DERR_INVALIDCALL);auto r=make(Kind::stateblock);r->saved=std::make_shared<State>(state);return out_res(a[2],r);}
    case 64:if(a[1]>=16)return U32(D3DERR_INVALIDCALL);return out_res(a[2],state.textures[a[1]]);
    case 65:{if(a[1]>=16)return U32(D3DERR_INVALIDCALL);set_texture(a[1],get(a[2],Kind::texture,true),a[2]);return 0;}
    case 66:case 67:if(a[1]>=8 || a[2]>=33)return U32(D3DERR_INVALIDCALL);if(slot==66)return output(a[3],state.stage[a[1]][a[2]]);set_word(state.stage[a[1]][a[2]],a[3],baseline_words.stage[a[1]][a[2]]);return 0;
    case 68:case 69:if(a[1]>=16 || a[2]>=14)return U32(D3DERR_INVALIDCALL);if(slot==68)return output(a[3],state.sampler[a[1]][a[2]]);set_word(state.sampler[a[1]][a[2]],a[3],baseline_words.sampler[a[1]][a[2]]);return 0;
    case 70:return output(a[1],U32(1));case 75:{const auto scissor=read<RECT>(p,a[1]);if(std::memcmp(&state.scissor,&scissor,sizeof(scissor)))++config_version;state.scissor=scissor;return 0;}case 76:return output(a[1],state.scissor);
    case 77:return a[1]?0:U32(D3DERR_INVALIDCALL);case 78:return 1;
    case 86:{auto r=make(Kind::declaration);for(unsigned n=0;n<65;++n){auto e=read<D3DVERTEXELEMENT9>(p,a[1]+n*8);r->elements.push_back(e);if(e.Stream==0xff)return out_res(a[2],r);}stop("unterminated vertex declaration");}
    case 87:{auto declaration=get(a[1],Kind::declaration,true);if(state.declaration!=declaration || state.fvf)++config_version;state.declaration=std::move(declaration);state.fvf=0;bind_same_states();return 0;}case 88:return out_res(a[1],state.declaration);
    case 89:if(state.fvf!=a[1] || state.declaration)++config_version;state.fvf=a[1];state.declaration.reset();bind_same_states();return 0;case 90:return output(a[1],state.fvf);
    case 91:case 106:return compile(a[1],a[2],slot==91?Kind::vertexshader:Kind::pixelshader);
    case 92:{auto shader=get(a[1],Kind::vertexshader,true);if(state.vertex_shader!=shader)++config_version;state.vertex_shader=std::move(shader);return 0;}case 93:return out_res(a[1],state.vertex_shader);
    case 107:{auto shader=get(a[1],Kind::pixelshader,true);if(state.pixel_shader!=shader)++config_version;state.pixel_shader=std::move(shader);return 0;}case 108:return out_res(a[1],state.pixel_shader);
    case 100:if(a[1]>=16)return U32(D3DERR_INVALIDCALL);state.streams[a[1]]={get(a[2],Kind::vertexbuffer,true),a[3],a[4],1};return 0;
    case 101:if(a[1]>=16)return U32(D3DERR_INVALIDCALL);output(a[3],state.streams[a[1]].offset);output(a[4],state.streams[a[1]].stride);return out_res(a[2],state.streams[a[1]].buffer);
    case 102:if(a[1]>=16 || a[2]!=1)return U32(D3DERR_INVALIDCALL);state.streams[a[1]].frequency=a[2];return 0;case 103:if(a[1]>=16)return U32(D3DERR_INVALIDCALL);return output(a[2],state.streams[a[1]].frequency);
    case 104:state.indices=get(a[1],Kind::indexbuffer,true);return 0;case 105:return out_res(a[1],state.indices);
    default:break;
    }
    if((slot>=94 && slot<=99) || (slot>=109 && slot<=114)){
        const bool pixel=slot>=109;const auto mode=slot-(pixel?109:94);const bool writing=mode%2==0;const auto type=mode/2;const auto units=type==2?1u:4u;const auto limit=type==0?256u:16u;if(a[1]>=limit || a[3]>limit-a[1])return U32(D3DERR_INVALIDCALL);void* target=nullptr;
        if(type==0)target=pixel?static_cast<void*>(state.pixel_f[a[1]].data()):static_cast<void*>(state.vertex_f[a[1]].data());else if(type==1)target=pixel?static_cast<void*>(state.pixel_i[a[1]].data()):static_cast<void*>(state.vertex_i[a[1]].data());else target=pixel?static_cast<void*>(state.pixel_b.data()+a[1]):static_cast<void*>(state.vertex_b.data()+a[1]);const auto count=std::size_t(a[3])*units*4;
        if(writing){p.memory.copy_out(a[2],std::span(static_cast<std::uint8_t*>(target),count));++config_version;}else p.memory.copy_in(a[2],std::span(static_cast<const std::uint8_t*>(target),count));return 0;
    }
    // Draw calls are staged below, after their guest pointers have been checked.
    if(slot==81 || slot==82 || slot==83 || slot==84){
        const auto count=[](U32 type,U32 prim)->U32 {if(prim>1048575)return 0;switch(type){case D3DPT_POINTLIST:return prim;case D3DPT_LINELIST:return prim*2;case D3DPT_LINESTRIP:return prim?prim+1:0;case D3DPT_TRIANGLELIST:return prim*3;case D3DPT_TRIANGLESTRIP:case D3DPT_TRIANGLEFAN:return prim?prim+2:0;default:return 0;}};
        const auto type=a[1],primitives=slot==81?a[3]:slot==82?a[6]:slot==83?a[2]:a[4];const auto n=count(type,primitives);if(primitives && !n)return U32(D3DERR_INVALIDCALL);
        if(slot==83){const auto size=std::size_t(n)*a[4];if(size>MaxBytes)stop("staging limit");up_vertex_bytes.resize(size);
            if(size)p.memory.copy_out(a[3],up_vertex_bytes);
            const auto hr=draw(type,n,up_vertex_bytes,a[4]);state.streams[0]={};return hr;
        }
        std::vector<std::uint8_t> vertices;std::vector<U32> indices;U32 stride=0;
        if(slot==84){stride=a[8];if(std::uint64_t(a[2])+a[3]>1048576)return U32(D3DERR_INVALIDCALL);vertices=array<std::uint8_t>(a[7],std::size_t(a[2]+a[3])*stride);const auto size=a[6]==D3DFMT_INDEX16?2:a[6]==D3DFMT_INDEX32?4:0;if(!size)return U32(D3DERR_INVALIDCALL);auto bytes=array<std::uint8_t>(a[5],std::size_t(n)*size);for(U32 j=0;j<n;++j){U32 v=0;std::memcpy(&v,bytes.data()+j*size,size);if(v<a[2] || std::uint64_t(v)>=std::uint64_t(a[2])+a[3])return U32(D3DERR_INVALIDCALL);indices.push_back(v);}}
        else {const auto& s=state.streams[0];if(!s.buffer || !s.stride || s.offset>s.buffer->bytes.size())return U32(D3DERR_INVALIDCALL);stride=s.stride;vertices.assign(s.buffer->bytes.begin()+s.offset,s.buffer->bytes.end());
            if(slot==81){const auto offset=std::size_t(a[2])*stride;if(offset>vertices.size())return U32(D3DERR_INVALIDCALL);vertices.erase(vertices.begin(),vertices.begin()+offset);if(std::size_t(n)*stride>vertices.size())return U32(D3DERR_INVALIDCALL);vertices.resize(std::size_t(n)*stride);}
            else {if(!state.indices)return U32(D3DERR_INVALIDCALL);const auto size=state.indices->format==D3DFMT_INDEX16?2:state.indices->format==D3DFMT_INDEX32?4:0;const auto& bytes=state.indices->bytes;if(!size || (std::uint64_t(a[5])+n)*size>bytes.size())return U32(D3DERR_INVALIDCALL);for(U32 j=0;j<n;++j){U32 v=0;std::memcpy(&v,bytes.data()+std::size_t(a[5]+j)*size,size);const auto adjusted=std::int64_t(std::bit_cast<std::int32_t>(a[2]))+v;if(v<a[3] || std::uint64_t(v)>=std::uint64_t(a[3])+a[4] || adjusted<0 || (std::uint64_t(adjusted)+1)*stride>vertices.size())return U32(D3DERR_INVALIDCALL);indices.push_back(U32(adjusted));}}}
        const auto hr=draw(type,n,vertices,stride,indices);if(slot==83 || slot==84)state.streams[0]={};if(slot==84)state.indices.reset();return hr;
    }
    stop(std::string("unimplemented Device::")+methods_device[slot].name);
}
void Graphics::bind_sampler(unsigned n,const Res& texture){
    const auto& s=state.sampler[n];for(const auto& level:texture->levels)upload(level);
    const auto object=image(texture)->texture;
    gl.ActiveTexture(GL_TEXTURE0+n);glBindTexture(GL_TEXTURE_2D,object->id);
    const auto address=[&](U32 mode)->GLint{switch(mode){case D3DTADDRESS_WRAP:return GL_REPEAT;case D3DTADDRESS_MIRROR:return GL_MIRRORED_REPEAT;case D3DTADDRESS_CLAMP:return GL_CLAMP_TO_EDGE;case D3DTADDRESS_BORDER:return GL_CLAMP_TO_BORDER;default:stop("sampler address mode");}};
    GLint min=s[D3DSAMP_MINFILTER]==D3DTEXF_POINT?GL_NEAREST:GL_LINEAR;
    if(s[D3DSAMP_MIPFILTER] && texture->levels.size()>1)min=s[D3DSAMP_MINFILTER]==D3DTEXF_POINT?(s[D3DSAMP_MIPFILTER]==D3DTEXF_POINT?GL_NEAREST_MIPMAP_NEAREST:GL_NEAREST_MIPMAP_LINEAR):(s[D3DSAMP_MIPFILTER]==D3DTEXF_POINT?GL_LINEAR_MIPMAP_NEAREST:GL_LINEAR_MIPMAP_LINEAR);
    const Texture::Parameters requested{address(s[D3DSAMP_ADDRESSU]),address(s[D3DSAMP_ADDRESSV]),min,
        s[D3DSAMP_MAGFILTER]==D3DTEXF_POINT?GL_NEAREST:GL_LINEAR,s[D3DSAMP_BORDERCOLOR]};
    const auto previous=sampler_cache_enabled?object->parameters:std::nullopt;
    auto parameter=[&](GLenum name,GLint value,GLint old){if(!previous || value!=old){glTexParameteri(GL_TEXTURE_2D,name,value);++sampler_parameter_calls;}else ++sampler_parameter_skips;};
    parameter(GL_TEXTURE_WRAP_S,requested.s,previous?previous->s:0);
    parameter(GL_TEXTURE_WRAP_T,requested.t,previous?previous->t:0);
    parameter(GL_TEXTURE_MIN_FILTER,requested.min,previous?previous->min:0);
    parameter(GL_TEXTURE_MAG_FILTER,requested.mag,previous?previous->mag:0);
    if(!previous || requested.border!=previous->border){
        const auto color=requested.border;const std::array<float,4> border{float((color>>16)&255)/255,float((color>>8)&255)/255,float(color&255)/255,float(color>>24)/255};
        glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,border.data());++sampler_parameter_calls;
    }else ++sampler_parameter_skips;
    // Parameters belong to the shared GL object, not the D3D binding slot.
    // Recording the actual last applied state retains ordered alias behavior.
    object->parameters=requested;
}
void Graphics::apply(){
    FrameTrace::Timer timing(frame_trace.enabled?&frame_trace.apply:nullptr);
    if(!pending_vertices.empty() && (!applied || config_version!=applied_config_version || word_differences || texture_differences))flush();
    auto target=image(state.target);upload(target);
    bool texture_environment_ready=bool(applied);
    if(applied && texture_differences)for(unsigned unit=0;unit<8;++unit)if(state.textures[unit] && !applied->textures[unit])texture_environment_ready=false;
    if(texture_environment_ready && !state.vertex_shader && !state.pixel_shader && config_version==applied_config_version && !word_differences){
        if(texture_differences){
            bool active=true;
            for(unsigned unit=0;unit<8;++unit){
                if(state.stage[unit][D3DTSS_COLOROP]==D3DTOP_DISABLE)active=false;
                if(state.textures[unit]!=applied->textures[unit]){gl.ActiveTexture(GL_TEXTURE0+unit);
                    if(active && state.textures[unit])glEnable(GL_TEXTURE_2D);else glDisable(GL_TEXTURE_2D);}
                // Parameters belong to GL texture objects. Reapply in unit
                // order even for unchanged aliases so the previous behavior
                // remains exact when one image is bound to several units.
                if(const auto& texture=state.textures[unit])bind_sampler(unit,texture);
            }
            gl.ActiveTexture(GL_TEXTURE0);applied->textures=state.textures;baseline_textures=state.textures;texture_differences=0;applied_texture_version=texture_version;++texture_only_applies;
        }else for(unsigned unit=0;unit<8;++unit)if(const auto& texture=state.textures[unit])for(const auto& level:texture->levels)upload(level);
        ++state_cache_hits;return;
    }
    ++state_applies;fbo(target);GLuint depth_id{};
    if(state.depth){auto d=image(state.depth);if(d->width!=target->width || d->height!=target->height)stop("depth/target size mismatch");depth_id=d->texture->id;}
    if(target->attached_depth!=depth_id){gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_TEXTURE_2D,depth_id,0);target->attached_depth=depth_id;
        ++framebuffer_checks;if(gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)stop("incomplete depth framebuffer");}
    const auto& v=state.viewport;glViewport(v.X,v.Y,v.Width,v.Height);glDepthRange(v.MinZ,v.MaxZ);
    auto enable=[](GLenum cap,bool on){if(on)glEnable(cap);else glDisable(cap);};const auto& r=state.render;
    enable(GL_DEPTH_TEST,r[D3DRS_ZENABLE] && bool(state.depth));glDepthMask(r[D3DRS_ZWRITEENABLE]!=0);glDepthFunc(GLenum(GL_NEVER+r[D3DRS_ZFUNC]-1));
    enable(GL_CULL_FACE,r[D3DRS_CULLMODE]!=D3DCULL_NONE);glCullFace(GL_BACK);glFrontFace(r[D3DRS_CULLMODE]==D3DCULL_CW?GL_CW:GL_CCW);
    glPolygonMode(GL_FRONT_AND_BACK,r[D3DRS_FILLMODE]==D3DFILL_WIREFRAME?GL_LINE:r[D3DRS_FILLMODE]==D3DFILL_POINT?GL_POINT:GL_FILL);
    glShadeModel(r[D3DRS_SHADEMODE]==D3DSHADE_FLAT?GL_FLAT:GL_SMOOTH);glDisable(GL_LIGHTING);glDisable(GL_FOG);
    enable(GL_SCISSOR_TEST,r[D3DRS_SCISSORTESTENABLE]!=0);glScissor(state.scissor.left,state.scissor.top,state.scissor.right-state.scissor.left,state.scissor.bottom-state.scissor.top);
    enable(GL_ALPHA_TEST,r[D3DRS_ALPHATESTENABLE]!=0);glAlphaFunc(GLenum(GL_NEVER+r[D3DRS_ALPHAFUNC]-1),float(r[D3DRS_ALPHAREF]&255)/255);
    const auto mask=r[D3DRS_COLORWRITEENABLE];glColorMask(mask&1,mask&2,mask&4,mask&8);enable(GL_BLEND,r[D3DRS_ALPHABLENDENABLE]!=0);
    auto blend=[&](U32 b)->GLenum{switch(b){case D3DBLEND_ZERO:return GL_ZERO;case D3DBLEND_ONE:return GL_ONE;case D3DBLEND_SRCCOLOR:return GL_SRC_COLOR;case D3DBLEND_INVSRCCOLOR:return GL_ONE_MINUS_SRC_COLOR;case D3DBLEND_SRCALPHA:case D3DBLEND_BOTHSRCALPHA:return GL_SRC_ALPHA;case D3DBLEND_INVSRCALPHA:case D3DBLEND_BOTHINVSRCALPHA:return GL_ONE_MINUS_SRC_ALPHA;case D3DBLEND_DESTALPHA:return GL_DST_ALPHA;case D3DBLEND_INVDESTALPHA:return GL_ONE_MINUS_DST_ALPHA;case D3DBLEND_DESTCOLOR:return GL_DST_COLOR;case D3DBLEND_INVDESTCOLOR:return GL_ONE_MINUS_DST_COLOR;case D3DBLEND_SRCALPHASAT:return GL_SRC_ALPHA_SATURATE;case D3DBLEND_BLENDFACTOR:return GL_CONSTANT_COLOR;case D3DBLEND_INVBLENDFACTOR:return GL_ONE_MINUS_CONSTANT_COLOR;default:stop("blend factor "+std::to_string(b));}};
    auto equation=[&](U32 op)->GLenum{switch(op){case D3DBLENDOP_ADD:return GL_FUNC_ADD;case D3DBLENDOP_SUBTRACT:return GL_FUNC_SUBTRACT;case D3DBLENDOP_REVSUBTRACT:return GL_FUNC_REVERSE_SUBTRACT;case D3DBLENDOP_MIN:return GL_MIN;case D3DBLENDOP_MAX:return GL_MAX;default:stop("blend equation");}};
    const auto color=r[D3DRS_BLENDFACTOR];gl.BlendColor(float((color>>16)&255)/255,float((color>>8)&255)/255,float(color&255)/255,float(color>>24)/255);
    const auto src=blend(r[D3DRS_SRCBLEND]),dst=blend(r[D3DRS_DESTBLEND]);gl.BlendFuncSeparate(src,dst,r[D3DRS_SEPARATEALPHABLENDENABLE]?blend(r[D3DRS_SRCBLENDALPHA]):src,r[D3DRS_SEPARATEALPHABLENDENABLE]?blend(r[D3DRS_DESTBLENDALPHA]):dst);gl.BlendEquationSeparate(equation(r[D3DRS_BLENDOP]),equation(r[D3DRS_SEPARATEALPHABLENDENABLE]?r[D3DRS_BLENDOPALPHA]:r[D3DRS_BLENDOP]));
    const auto vs=state.vertex_shader?state.vertex_shader->shader:nullptr,ps=state.pixel_shader?state.pixel_shader->shader:nullptr;
    if(vs || ps){auto key=std::make_pair(vs,ps);auto it=programs.find(key);if(it==programs.end()){auto* program=MOJOSHADER_glLinkProgram(vs,ps);if(!program)stop(std::string("shader link: ")+MOJOSHADER_glGetError());it=programs.emplace(key,program).first;}MOJOSHADER_glBindProgram(it->second);
        MOJOSHADER_glSetVertexShaderUniformF(0,state.vertex_f[0].data(),256);MOJOSHADER_glSetPixelShaderUniformF(0,state.pixel_f[0].data(),256);MOJOSHADER_glSetVertexShaderUniformI(0,state.vertex_i[0].data(),16);MOJOSHADER_glSetPixelShaderUniformI(0,state.pixel_i[0].data(),16);MOJOSHADER_glSetVertexShaderUniformB(0,state.vertex_b.data(),16);MOJOSHADER_glSetPixelShaderUniformB(0,state.pixel_b.data(),16);
    }else MOJOSHADER_glBindProgram(nullptr);
    bool active=true;for(unsigned n=0;n<8;++n){const auto& t=state.stage[n];if(t[D3DTSS_COLOROP]==D3DTOP_DISABLE)active=false;gl.ActiveTexture(GL_TEXTURE0+n);const auto texture=state.textures[n];const bool used=texture && (ps || active);enable(GL_TEXTURE_2D,used);if(!texture)continue;
        bind_sampler(n,texture);
        const auto color=r[D3DRS_TEXTUREFACTOR];const std::array<float,4> factor{float((color>>16)&255)/255,float((color>>8)&255)/255,float(color&255)/255,float(color>>24)/255};glTexEnvfv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_COLOR,factor.data());
        if(ps || !active)continue;glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);
        auto combine=[&](bool alpha,U32 op,U32 arg1,U32 arg2){GLenum mode=GL_MODULATE;GLint scale=1;if(op==D3DTOP_SELECTARG1 || op==D3DTOP_SELECTARG2)mode=GL_REPLACE;else if(op==D3DTOP_ADD)mode=GL_ADD;else if(op==D3DTOP_SUBTRACT)mode=GL_SUBTRACT;else if(op==D3DTOP_MODULATE2X)scale=2;else if(op==D3DTOP_MODULATE4X)scale=4;else if(op==D3DTOP_BLENDTEXTUREALPHA)mode=GL_INTERPOLATE;else if(op!=D3DTOP_MODULATE && op!=D3DTOP_DISABLE)stop("texture operation "+std::to_string(op));
            glTexEnvi(GL_TEXTURE_ENV,alpha?GL_COMBINE_ALPHA:GL_COMBINE_RGB,mode);glTexEnvi(GL_TEXTURE_ENV,alpha?GL_ALPHA_SCALE:GL_RGB_SCALE,scale);if(op==D3DTOP_SELECTARG2)arg1=arg2;
            unsigned j=0;for(auto arg:{arg1,arg2}){GLint source=GL_PREVIOUS;switch(arg&D3DTA_SELECTMASK){case D3DTA_CURRENT:source=n?GL_PREVIOUS:GL_PRIMARY_COLOR;break;case D3DTA_DIFFUSE:source=GL_PRIMARY_COLOR;break;case D3DTA_TEXTURE:source=GL_TEXTURE;break;case D3DTA_TFACTOR:source=GL_CONSTANT;break;default:stop("texture argument");}const bool a=alpha || (arg&D3DTA_ALPHAREPLICATE);glTexEnvi(GL_TEXTURE_ENV,(alpha?GL_SOURCE0_ALPHA:GL_SOURCE0_RGB)+j,source);glTexEnvi(GL_TEXTURE_ENV,(alpha?GL_OPERAND0_ALPHA:GL_OPERAND0_RGB)+j,a?((arg&D3DTA_COMPLEMENT)?GL_ONE_MINUS_SRC_ALPHA:GL_SRC_ALPHA):((arg&D3DTA_COMPLEMENT)?GL_ONE_MINUS_SRC_COLOR:GL_SRC_COLOR));++j;}
            if(mode==GL_INTERPOLATE){glTexEnvi(GL_TEXTURE_ENV,alpha?GL_SOURCE2_ALPHA:GL_SOURCE2_RGB,GL_TEXTURE);glTexEnvi(GL_TEXTURE_ENV,alpha?GL_OPERAND2_ALPHA:GL_OPERAND2_RGB,GL_SRC_ALPHA);}
        };
        combine(false,t[D3DTSS_COLOROP],t[D3DTSS_COLORARG1],t[D3DTSS_COLORARG2]);combine(true,t[D3DTSS_ALPHAOP],t[D3DTSS_ALPHAARG1],t[D3DTSS_ALPHAARG2]);
        glMatrixMode(GL_TEXTURE);if(t[D3DTSS_TEXTURETRANSFORMFLAGS])glLoadMatrixf(&state.transforms[D3DTS_TEXTURE0+n].m[0][0]);else glLoadIdentity();
    }
    gl.ActiveTexture(GL_TEXTURE0);glMatrixMode(GL_MODELVIEW);glLoadIdentity();glMatrixMode(GL_PROJECTION);glLoadIdentity();
    if((state.fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW)glOrtho(v.X,double(v.X)+v.Width,v.Y,double(v.Y)+v.Height,0,-1);
    if(!state.vertex_shader && !state.pixel_shader){applied=state;applied_config_version=config_version;applied_texture_version=texture_version;reset_baseline();}
}
void Graphics::flush() noexcept {
    if(pending_vertices.empty())return;
    FrameTrace::Timer timing(frame_trace.enabled?&frame_trace.flush:nullptr);
    if(!client_arrays_valid){for(unsigned unit=0;unit<8;++unit){gl.ClientActiveTexture(GL_TEXTURE0+unit);glDisableClientState(GL_TEXTURE_COORD_ARRAY);}client_coordinates.fill(false);}
    const auto& first=pending_vertices.front();const auto stride=GLsizei(sizeof(FixedVertex));
    glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(4,GL_FLOAT,stride,first.pos.data());glColorPointer(4,GL_FLOAT,stride,first.color.data());
    for(unsigned unit=0;unit<8;++unit){
        if(pending_coordinates[unit]){gl.ClientActiveTexture(GL_TEXTURE0+unit);if(!client_coordinates[unit])glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(4,GL_FLOAT,stride,first.uv[pending_coordinate_indices[unit]].data());}
        else if(client_coordinates[unit]){gl.ClientActiveTexture(GL_TEXTURE0+unit);glDisableClientState(GL_TEXTURE_COORD_ARRAY);}
    }
    client_coordinates=pending_coordinates;client_arrays_valid=true;gl.ClientActiveTexture(GL_TEXTURE0);
    glDrawArrays(GL_TRIANGLES,0,GLsizei(pending_vertices.size()));++submitted_draws;submitted_vertices+=pending_vertices.size();
    if(pending_target)pending_target->levels[0]->gpu_dirty=true;
    pending_vertices.clear();pending_target.reset();
}
U32 Graphics::draw(U32 type,U32 count,std::span<const std::uint8_t> bytes,U32 stride,std::span<const U32> indices){
    FrameTrace::Timer timing(frame_trace.enabled?&frame_trace.draw:nullptr);
    if(!state.scene || !stride || stride>256 || bytes.size()%stride)return U32(D3DERR_INVALIDCALL);if(!count)return 0;const auto total=bytes.size()/stride;
    if((indices.empty() && count>total) || (!indices.empty() && indices.size()!=count))return U32(D3DERR_INVALIDCALL);
    using Vertex=DecodedVertex;
    decoded_vertices.assign(total,Vertex{});auto& vertices=decoded_vertices;
    auto f32=[](const std::uint8_t* at){float v;std::memcpy(&v,at,4);return v;};
    auto read_color=[](const std::uint8_t* at){return std::array<float,4>{float(at[2])/255,float(at[1])/255,float(at[0])/255,float(at[3])/255};};
    for(std::size_t n=0;n<total;++n){auto& v=vertices[n];const auto* base=bytes.data()+n*stride;
        if(state.declaration){for(const auto& e:state.declaration->elements){if(e.Stream==0xff)break;if(e.Stream || e.Method!=D3DDECLMETHOD_DEFAULT)stop("vertex declaration stream/method");std::array<float,4>* dst=nullptr;if(e.Usage==D3DDECLUSAGE_POSITION || e.Usage==D3DDECLUSAGE_POSITIONT)dst=&v.pos;else if(e.Usage==D3DDECLUSAGE_NORMAL)dst=&v.normal;else if(e.Usage==D3DDECLUSAGE_COLOR)dst=e.UsageIndex?&v.specular:&v.color;else if(e.Usage==D3DDECLUSAGE_TEXCOORD && e.UsageIndex<8)dst=&v.uv[e.UsageIndex];else stop("vertex declaration usage");const auto size=e.Type<=D3DDECLTYPE_FLOAT4?(e.Type+1)*4:e.Type==D3DDECLTYPE_D3DCOLOR?4:0;if(!size || U32(e.Offset)+size>stride)stop("vertex declaration type/bounds");if(e.Type==D3DDECLTYPE_D3DCOLOR)*dst=read_color(base+e.Offset);else for(unsigned j=0;j<size/4;++j)(*dst)[j]=f32(base+e.Offset+j*4);}}
        else {unsigned offset=0;auto floats=[&](float* dst,unsigned size){if(offset+size*4>stride)stop("FVF stride");for(unsigned j=0;j<size;++j)dst[j]=f32(base+offset+j*4);offset+=size*4;};const auto pos=state.fvf&D3DFVF_POSITION_MASK;if(pos!=D3DFVF_XYZ && pos!=D3DFVF_XYZRHW)stop("FVF position type");floats(v.pos.data(),pos==D3DFVF_XYZRHW?4:3);if(state.fvf&D3DFVF_NORMAL)floats(v.normal.data(),3);if(state.fvf&D3DFVF_PSIZE){float unused;floats(&unused,1);}for(auto flag:{D3DFVF_DIFFUSE,D3DFVF_SPECULAR})if(state.fvf&flag){if(offset+4>stride)stop("FVF color stride");(flag==D3DFVF_DIFFUSE?v.color:v.specular)=read_color(base+offset);offset+=4;}const auto tex=(state.fvf&D3DFVF_TEXCOUNT_MASK)>>D3DFVF_TEXCOUNT_SHIFT;if(tex>8)stop("FVF texture count");for(unsigned j=0;j<tex;++j){const auto code=(state.fvf>>(16+j*2))&3;floats(v.uv[j].data(),code==0?2:code==1?3:code==2?4:1);}}
    }
    for(auto index:indices)if(index>=total)return U32(D3DERR_INVALIDCALL);
    if(frame_trace.enabled && total<=6 && total>=3 && state.textures[0] && (state.fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW){
        float left=vertices[0].pos[0],right=left,top=vertices[0].pos[1],bottom=top;
        const auto alpha=U32(std::clamp(std::lround(vertices[0].color[3]*255),0l,255l));bool uniform=true;
        for(const auto& v:vertices){left=std::min(left,v.pos[0]);right=std::max(right,v.pos[0]);top=std::min(top,v.pos[1]);bottom=std::max(bottom,v.pos[1]);uniform&=std::abs(v.color[3]-vertices[0].color[3])<.001f;}
        if(uniform && (right-left)*(bottom-top)>=16384){const auto i=image(state.textures[0]);const auto factor=state.render[D3DRS_TEXTUREFACTOR];
            const auto key=i->texture->id;
            const auto value=std::pair{alpha,factor};const auto found=frame_trace.last_opacity.find(key);
            if(found==frame_trace.last_opacity.end() || found->second!=value){if(frame_trace.last_opacity.size()>=4096)frame_trace.last_opacity.clear();frame_trace.last_opacity[key]=value;frame_trace.opacity.push_back({presents+1,FrameTrace::counter(),i->texture->id,i->width,i->height,alpha,factor,left,top,right,bottom});if(frame_trace.opacity.size()>600)frame_trace.opacity.pop_front();}
        }
    }
    const bool batchable=!state.vertex_shader && !state.pixel_shader &&
        (type==D3DPT_TRIANGLELIST || type==D3DPT_TRIANGLESTRIP || type==D3DPT_TRIANGLEFAN);
    if(!batchable)flush();
    apply();const GLenum primitive=type==D3DPT_POINTLIST?GL_POINTS:type==D3DPT_LINELIST?GL_LINES:type==D3DPT_LINESTRIP?GL_LINE_STRIP:type==D3DPT_TRIANGLELIST?GL_TRIANGLES:type==D3DPT_TRIANGLESTRIP?GL_TRIANGLE_STRIP:GL_TRIANGLE_FAN;
    if(batchable){
        auto transform=[](std::array<float,4> v,const D3DMATRIX& m){std::array<float,4> out{};for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k)out[j]+=v[k]*m.m[k][j];return out;};
        auto vertex=[&](U32 index){const auto& v=vertices[indices.empty()?index:indices[index]];FixedVertex out{v.pos,v.color,v.uv};
            if((state.fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW){const float w=v.pos[3]?1/v.pos[3]:1;out.pos={(v.pos[0]+.5f)*w,(v.pos[1]+.5f)*w,v.pos[2]*w,w};}
            else {const auto pos=transform(transform(transform(v.pos,state.transforms[D3DTS_WORLD]),state.transforms[D3DTS_VIEW]),state.transforms[D3DTS_PROJECTION]);out.pos={pos[0],-pos[1],2*pos[2]-pos[3],pos[3]};}return out;};
        auto triangle=[&](U32 a,U32 b,U32 c){
            if(pending_vertices.size()+3>4096)flush();
            if(pending_vertices.empty()){pending_target=state.target;bool active=true;for(unsigned unit=0;unit<8;++unit){
                if(state.stage[unit][D3DTSS_COLOROP]==D3DTOP_DISABLE)active=false;pending_coordinates[unit]=active && bool(state.textures[unit]);
                const auto coordinate=state.stage[unit][D3DTSS_TEXCOORDINDEX]&0xffff;pending_coordinate_indices[unit]=coordinate<8?coordinate:0;}}
            pending_vertices.push_back(vertex(a));pending_vertices.push_back(vertex(b));pending_vertices.push_back(vertex(c));
        };
        if(type==D3DPT_TRIANGLELIST){for(U32 j=0;j+2<count;j+=3)triangle(j,j+1,j+2);}
        else if(type==D3DPT_TRIANGLESTRIP){for(U32 j=0;j+2<count;++j)triangle(j+(j&1),j+1-(j&1),j+2);}
        else for(U32 j=1;j+1<count;++j)triangle(0,j,j+1);
        ++batched_guest_draws;image(state.target)->gpu_dirty=true;++draws;return 0;
    }
    if(state.vertex_shader){auto& first=vertices[0];const unsigned step=sizeof(Vertex);MOJOSHADER_glSetVertexAttribute(MOJOSHADER_USAGE_POSITION,0,4,MOJOSHADER_ATTRIBUTE_FLOAT,0,step,first.pos.data());MOJOSHADER_glSetVertexAttribute(MOJOSHADER_USAGE_NORMAL,0,3,MOJOSHADER_ATTRIBUTE_FLOAT,0,step,first.normal.data());MOJOSHADER_glSetVertexAttribute(MOJOSHADER_USAGE_COLOR,0,4,MOJOSHADER_ATTRIBUTE_FLOAT,0,step,first.color.data());MOJOSHADER_glSetVertexAttribute(MOJOSHADER_USAGE_COLOR,1,4,MOJOSHADER_ATTRIBUTE_FLOAT,0,step,first.specular.data());for(int j=0;j<8;++j)MOJOSHADER_glSetVertexAttribute(MOJOSHADER_USAGE_TEXCOORD,j,4,MOJOSHADER_ATTRIBUTE_FLOAT,0,step,first.uv[j].data());MOJOSHADER_glProgramReady();MOJOSHADER_glProgramViewportInfo(state.viewport.Width,state.viewport.Height,image(backbuffer)->width,image(backbuffer)->height,1);if(indices.empty())glDrawArrays(primitive,0,count);else glDrawElements(primitive,count,GL_UNSIGNED_INT,indices.data());}
    else {
        if(state.pixel_shader){MOJOSHADER_glProgramReady();MOJOSHADER_glProgramViewportInfo(state.viewport.Width,state.viewport.Height,image(backbuffer)->width,image(backbuffer)->height,1);}
        auto transform=[](std::array<float,4> v,const D3DMATRIX& m){std::array<float,4> out{};for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k)out[j]+=v[k]*m.m[k][j];return out;};
        std::array<bool,8> coordinates{};bool active=true;for(unsigned unit=0;unit<8;++unit){if(state.stage[unit][D3DTSS_COLOROP]==D3DTOP_DISABLE)active=false;coordinates[unit]=bool(state.pixel_shader) || (active && bool(state.textures[unit]));}
        glBegin(primitive);for(U32 j=0;j<count;++j){const auto& v=vertices[indices.empty()?j:indices[j]];glColor4fv(v.color.data());for(unsigned unit=0;unit<8;++unit)if(coordinates[unit]){const auto coord=state.stage[unit][D3DTSS_TEXCOORDINDEX]&0xffff;const auto& uv=v.uv[coord<8?coord:0];gl.MultiTexCoord4f(GL_TEXTURE0+unit,uv[0],uv[1],uv[2],uv[3]);}
            if((state.fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW){const float w=v.pos[3]?1/v.pos[3]:1;glVertex4f((v.pos[0]+.5f)*w,(v.pos[1]+.5f)*w,v.pos[2]*w,w);}
            else {auto pos=transform(transform(transform(v.pos,state.transforms[D3DTS_WORLD]),state.transforms[D3DTS_VIEW]),state.transforms[D3DTS_PROJECTION]);glVertex4f(pos[0],-pos[1],2*pos[2]-pos[3],pos[3]);}
        }glEnd();
    }
    ++submitted_draws;client_arrays_valid=false;
    // glGetError is a synchronous round trip through some threaded drivers.
    // Validate all guest data/state before drawing, then collect GL errors at
    // the frame boundary instead of forcing a driver rendezvous per primitive.
    image(state.target)->gpu_dirty=true;++draws;return 0;
}
void Graphics::capture(bool force){
    if(p.state().snapshot_path.empty() || !draws || (!force && ::GetTickCount64()-last_capture<2000))return;last_capture=::GetTickCount64();auto i=image(backbuffer);download(i);
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+i->width*i->height*4;
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=LONG(i->width);info.biHeight=-LONG(i->height);info.biPlanes=1;info.biBitCount=32;info.biSizeImage=i->width*i->height*4;
    std::ofstream out(p.state().snapshot_path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(&header),sizeof(header));out.write(reinterpret_cast<const char*>(&info),sizeof(info));
    for(U32 y=0;y<i->height;++y)for(U32 x=0;x<i->width;++x){const auto c=decode(i->bytes.data()+(std::size_t(y)*i->width+x)*bpp(i->format),i->format);const std::array<std::uint8_t,4> bgra{c[2],c[1],c[0],c[3]};out.write(reinterpret_cast<const char*>(bgra.data()),4);}out.close();capture_result=out?S_OK:E_FAIL;if(out)++captures;
}
U32 Graphics::present(Args a){
    const auto trace_begin=frame_trace.enabled?FrameTrace::counter():0,trace_started=frame_trace.enabled?FrameTrace::now():0;
    applied.reset();
    if(a[4])stop("Present dirty regions");if(a[3] && a[3]!=(presentation[7]?presentation[7]:focus_window))stop("Present to another window");const bool ending=p.state().stop_after_presents && presents+1>=p.state().stop_after_presents;capture(ending);
    auto i=image(backbuffer);upload(i);gl.BindFramebuffer(GL_FRAMEBUFFER,0);int w=0,h=0;SDL_GL_GetDrawableSize(window,&w,&h);glViewport(0,0,w,h);MOJOSHADER_glBindProgram(nullptr);
    glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glColorMask(1,1,1,1);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    for(unsigned unit=0;unit<8;++unit){gl.ActiveTexture(GL_TEXTURE0+unit);glDisable(GL_TEXTURE_2D);}gl.ActiveTexture(GL_TEXTURE0);glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,i->texture->id);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glMatrixMode(GL_TEXTURE);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,w,0,h,-1,1);
    auto sr=rectangle(a[1],i);RECT dr=a[2]?read<RECT>(p,a[2]):RECT{0,0,w,h};const float u0=float(sr.left)/i->width,u1=float(sr.right)/i->width,v0=float(sr.top)/i->height,v1=float(sr.bottom)/i->height;
    i->texture->parameters.reset();
    glBegin(GL_QUADS);glTexCoord2f(u0,v1);glVertex2f(float(dr.left),float(h-dr.bottom));glTexCoord2f(u1,v1);glVertex2f(float(dr.right),float(h-dr.bottom));glTexCoord2f(u1,v0);glVertex2f(float(dr.right),float(h-dr.top));glTexCoord2f(u0,v0);glVertex2f(float(dr.left),float(h-dr.top));glEnd();
    const auto before_error=frame_trace.enabled?FrameTrace::now():0;
    ++frame_error_checks;const auto error=glGetError();if(error)stop("OpenGL frame error "+std::to_string(error));
    const auto before_swap=frame_trace.enabled?FrameTrace::now():0;
    SDL_GL_SwapWindow(window);
    const auto after_swap=frame_trace.enabled?FrameTrace::now():0;
    SDL_Event events[32];while(SDL_PeepEvents(events,32,SDL_GETEVENT,SDL_FIRSTEVENT,SDL_LASTEVENT)>0){}++presents;
    LARGE_INTEGER presented_at{};::QueryPerformanceCounter(&presented_at);DWORD foreground_pid{};::GetWindowThreadProcessId(::GetForegroundWindow(),&foreground_pid);
    recent_frames.push_back({presents,std::uint64_t(presented_at.QuadPart),foreground_pid==::GetCurrentProcessId(),bool(::IsIconic(host_window))});if(recent_frames.size()>600)recent_frames.pop_front();
    if(frame_trace.enabled){auto& t=frame_trace;t.recent.push_back({presents,std::uint64_t(presented_at.QuadPart),trace_begin,t.draw,t.apply,t.flush,t.upload,copy_total.total_ns-t.last_copy,FrameTrace::now()-trace_started,before_swap-before_error,after_swap-before_swap,draws-t.last_draws,uploads-t.last_uploads});if(t.recent.size()>600)t.recent.pop_front();t.draw=t.apply=t.flush=t.upload=0;t.last_copy=copy_total.total_ns;t.last_draws=draws;t.last_uploads=uploads;}
    if(presents==1 || presents==30 || presents==60 || presents==100 || presents==200)frame_times_ms[presents]=::GetTickCount64()-started_ms;if(ending){p.state().diagnostic_stop=true;p.exit(0);}return 0;
}
void Graphics::install(){
    p.state().reports["frame_timing"]=[this]{return frame_trace.report();};
    p.state().reports["resource_graphics"]=[this]{return "{\"surface_load_calls\":"+std::to_string(copy_total.calls)+
        ",\"pixels_processed\":"+std::to_string(copy_total.pixels)+",\"surface_load_ns\":"+std::to_string(copy_total.total_ns)+
        ",\"readback_ns\":"+std::to_string(copy_total.readback_ns)+",\"screen_capture\":false}";};
    api(p,"d3d9.dll","Direct3DCreate9",1,[this](Process&,Args a)->U32{enter();if(a[0]!=D3D_SDK_VERSION)return 0;if(!initialized){SDL_SetMainReady();if(SDL_InitSubSystem(SDL_INIT_VIDEO))stop(SDL_GetError());initialized=true;}if(!root)root=make(Kind::d3d);return wrap(root);});
    api(p,"d3dx9_41.dll","D3DXCreateTexture",8,[this](Process&,Args a){enter();get(a[0],Kind::device);p.memory.check(a[7],4,Memory::Write);return out_res(a[7],texture(a[1],a[2],a[3],a[4],a[5]==D3DFMT_UNKNOWN?D3DFMT_A8R8G8B8:D3DFORMAT(a[5]),a[6]));});
    api(p,"d3dx9_41.dll","D3DXLoadSurfaceFromMemory",10,[this](Process&,Args a)->U32{
        enter();if(!a[7])return U32(D3DERR_INVALIDCALL);auto dst=image(get(a[0],Kind::surface));const auto original=read<RECT>(p,a[7]);
        const bool indexed=a[4]==D3DFMT_P8 || a[4]==D3DFMT_A8P8;
        const auto size=indexed?(a[4]==D3DFMT_P8?1u:2u):bpp(D3DFORMAT(a[4]));
        if(!size || original.left<0 || original.top<0 || original.right<=original.left || original.bottom<=original.top || std::uint64_t(original.right)*size>a[5] || (indexed && !a[6]))return U32(D3DERR_INVALIDCALL);
        const U32 width=U32(original.right-original.left),height=U32(original.bottom-original.top),pitch=width*size;
        if(std::uint64_t(width)*height*(indexed?4:size)>MaxBytes)return U32(D3DERR_INVALIDCALL);
        const auto start=std::uint64_t(a[3])+std::uint64_t(original.top)*a[5]+std::uint64_t(original.left)*size;
        if(start+std::uint64_t(height-1)*a[5]+pitch>0x100000000ull)return U32(D3DERR_INVALIDCALL);
        if(p.options.profile_execution){source_bytes_before_crop+=std::uint64_t(original.bottom)*a[5];source_bytes_staged+=std::uint64_t(pitch)*height;}
        // Atlas tiles can have a large top/left offset. Read only the requested
        // rectangle; copying every preceding row repeatedly costs gigabytes
        // during startup and reads bytes outside the API's source rectangle.
        std::vector<std::uint8_t> data(std::size_t(pitch)*height);
        if(pitch==a[5])p.memory.copy_out(U32(start),data);
        else for(U32 y=0;y<height;++y)p.memory.copy_out(U32(start+std::uint64_t(y)*a[5]),std::span(data.data()+std::size_t(y)*pitch,pitch));
        const RECT sr{0,0,LONG(width),LONG(height)};
        if(indexed){
            const auto palette=read<std::array<PALETTEENTRY,256>>(p,a[6]);std::array<U32,256> colors{};
            for(std::size_t n=0;n<colors.size();++n){const auto& c=palette[n];colors[n]=U32(c.peRed)|(U32(c.peGreen)<<8)|(U32(c.peBlue)<<16)|(U32(c.peFlags)<<24);}
            std::vector<U32> rgba(std::size_t(width)*height);
            if(size==1)for(std::size_t n=0;n<rgba.size();++n)rgba[n]=colors[data[n]];
            else for(std::size_t n=0;n<rgba.size();++n)rgba[n]=(colors[data[n*2]]&0xffffffu)|(U32(data[n*2+1])<<24);
            return copy(dst,rectangle(a[2],dst),reinterpret_cast<const std::uint8_t*>(rgba.data()),width*4,D3DFMT_A8B8G8R8,sr,a[8],a[9]);
        }
        return copy(dst,rectangle(a[2],dst),data.data(),pitch,D3DFORMAT(a[4]),sr,a[8],a[9]);
    });
    api(p,"d3dx9_41.dll","D3DXLoadSurfaceFromSurface",8,[this](Process&,Args a)->U32{
        enter();if(a[1] || a[4])stop("palette conversion");auto src=image(get(a[3],Kind::surface)),dst=image(get(a[0],Kind::surface));auto sr=rectangle(a[5],src);if(!valid(sr,src->width,src->height))return U32(D3DERR_INVALIDCALL);download(src);auto data=src->bytes;return copy(dst,rectangle(a[2],dst),data.data(),src->width*bpp(src->format),src->format,sr,a[6],a[7]);
    });
    api(p,"d3dx9_41.dll","D3DXAssembleShader",7,[this](Process&,Args a)->U32{
        enter();if(a[3])stop("shader include callbacks");if(a[1]>1048576)stop("shader source limit");for(auto out:{a[5],a[6]})if(out){p.memory.check(out,4,Memory::Write);p.memory.store(out,0,32);}
        auto text=array<char>(a[0],a[1]);std::deque<std::string> strings;std::vector<MOJOSHADER_preprocessorDefine> defines;
        if(a[2])for(unsigned n=0;n<65;++n){const auto pair=read<std::array<U32,2>>(p,a[2]+n*8);if(!pair[0])break;if(n==64)stop("shader macro count limit");strings.push_back(p.read_string(pair[0]));const char* name=strings.back().c_str();strings.push_back(pair[1]?p.read_string(pair[1]):"");defines.push_back({name,strings.back().c_str()});}
        const auto* result=MOJOSHADER_assemble("guest-shader",text.data(),a[1],nullptr,0,nullptr,0,defines.data(),unsigned(defines.size()),nullptr,nullptr,nullptr,nullptr,nullptr);
        if(!result || result==&WA2_assembler_MOJOSHADER_out_of_mem_data)return U32(E_OUTOFMEMORY);struct Parsed {const MOJOSHADER_parseData* p;~Parsed(){MOJOSHADER_freeParseData(p);}} owner{result};
        if(result->error_count){std::ostringstream errors;for(int n=0;n<result->error_count;++n)errors<<result->errors[n].error<<'\n';if(a[6]){auto r=make(Kind::buffer);const auto s=errors.str();r->bytes.assign(s.begin(),s.end());r->bytes.push_back(0);out_res(a[6],r);}return U32(0x88760b59u);}
        if(a[5]){auto r=make(Kind::buffer);r->bytes.assign(result->output,result->output+result->output_len);out_res(a[5],r);}return 0;
    });
}
} // namespace
void install_wa2_direct3d(Process& p){auto state=std::make_shared<Graphics>(p);state->install();p.state().shutdown.push_back([state]{state->shutdown();});p.state().reports["direct3d9"]=[state]{return state->report();};p.state().reports["sdl_sampler_cache"]=[state]{return state->sampler_report();};p.state().reports["sdl_draw_batches"]=[state]{return state->batch_report();};p.state().reports["sdl_resource_lifetimes"]=[state]{return state->lifetime_report();};}
} // namespace wr
