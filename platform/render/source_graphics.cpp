// Shared source D3D9 ABI transport. Derived from the existing SDL backend;
// desktop GL and GLES3 execute the same state/resource/draw implementation.
#define SDL_MAIN_HANDLED
#include "surface_host.hpp"
#include "fixed_pipeline.hpp"
#include "shader_source.hpp"
#include "mojo_hooks.h"
#include "movie_presenter.hpp"
#include "movie_layout.hpp"
#include "frame_layout.hpp"
#include "submission_cache.hpp"
#include "screen_vertices.hpp"
#include "texture_batch.hpp"
#include "frame_metrics.hpp"
#include "../../recompiled/native_sprites.hpp"
#include "pixel_transfer.hpp"
#include "source_pixels.hpp"
#include "gl_api.hpp"
#include <mojoshader_assembler.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <sstream>
#include <thread>
#include "../../port/guest_api.hpp"
#include "../../port/weak_registry.hpp"
#include "../compat/interface_handles.hpp"
extern "C" MOJOSHADER_parseData WA2_assembler_MOJOSHADER_out_of_mem_data;
namespace wr { namespace {
using namespace port;
using namespace wa2::graphics::abi;
using wa2::graphics::GlApi;
struct POINT {std::int32_t x,y;};
std::uint64_t milliseconds(){return std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
enum class Kind {unknown,d3d,device,texture,surface,vertexbuffer,indexbuffer,vertexshader,pixelshader,declaration,stateblock,swapchain,buffer,base_texture,resource};
#include "../../port/d3d9_tables.inc"
constexpr std::size_t MaxBytes=128u*1024*1024;
float real(U32 v){return std::bit_cast<float>(v);}
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
    U32 data{},interface_handle{};std::vector<std::uint8_t> bytes;std::vector<std::shared_ptr<ImageData>> levels;
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
class Graphics final:public wa2::graphics::MoviePresenter {
    Process& p;wa2::platform::compat::InterfaceHandles handles;GlApi gl;SDL_Window* window{};SDL_GLContext context{};MOJOSHADER_glContext* mojo{};
    std::shared_ptr<wa2::graphics::SurfaceHost> host;
    GLuint vertex_buffer{},index_buffer{};
    MOJOSHADER_glShader* default_vertex{};
    MOJOSHADER_glShader* movie_shader{};std::array<GLuint,3> movie_planes{};
    std::array<std::pair<int,int>,3> movie_dimensions{};
    std::map<std::array<U32,65>,MOJOSHADER_glShader*> fixed_pixels;
    struct Program {MOJOSHADER_glProgram* shader{};GLint alpha_mode{-1},alpha_reference{-1};};
    std::string transformed_source,transform_error;
    MOJOSHADER_glShader* assemble_shader(const std::string&);
    MOJOSHADER_glShader* fixed_pixel(bool texture_batch=false);
    void prepare_program();
    void submit(GLenum,std::span<const DecodedVertex>,std::span<const U32>);
    DecodedVertex fixed_vertex(const DecodedVertex&);
    std::map<U32,Object> objects;std::map<Kind,U32> vtables;WeakRegistry<Resource> resources;
    WeakRegistry<Texture> textures_alive;WeakRegistry<ImageData> images;
    Res root,device_resource,backbuffer;State state;std::optional<State> recording,applied;
    std::optional<wa2::aot::SpriteService> sprite_service;
    bool sprite_command(const wa2::aot::SpriteCommand&);
    std::array<U32,14> presentation{};U32 focus_window{},creation_flags{};bool stopped{},initialized{};
    std::array<int,4> last_present_source{},last_present_destination{};std::array<int,2> last_drawable_size{};
    std::uint64_t movie_frames{};std::array<int,2> last_movie_client{},last_movie_size{};
    std::array<int,4> last_movie_source{},last_movie_requested{};std::array<double,4> last_movie_destination{};
    const std::thread::id thread{std::this_thread::get_id()};GLint max_texture{2048};
    std::uint64_t created{},devices{},textures{},presents{},draws{},uploads{},captures{},last_capture{};
    std::uint64_t state_applies{},state_cache_hits{},framebuffer_checks{},texture_only_applies{};
    std::uint64_t sampler_parameter_calls{},sampler_parameter_skips{},frame_error_checks{};
    std::uint64_t context_rebinds{},context_reuses{},context_free_calls{};
    bool sampler_cache_enabled{true};
    int requested_swap_interval{},actual_swap_interval{},swap_interval_result{};
    std::vector<FixedVertex> pending_vertices;Res pending_target;
    std::vector<wa2::graphics::ScreenVertex> pending_screen,screen_scratch;
    std::vector<std::uint16_t> pending_screen_indices;
    wa2::graphics::ScreenTextureBatch<Res> texture_batch;
    bool texture_batch_available{},texture_batch_mode{};
    std::string texture_batch_failure;
    std::uint64_t texture_batch_draws{},texture_batch_submissions{};
    std::uint64_t screen_draws{},screen_unique_vertices{},vertex_upload_bytes{},index_upload_bytes{};
    bool has_pending()const{return !pending_vertices.empty() || !pending_screen.empty() || !texture_batch.empty();}
    // Graphics methods stay on one owner thread and do not call guest code.
    // Queued draws own their FixedVertex copies, so these staging buffers can
    // retain capacity across successive DrawPrimitiveUP calls.
    std::vector<std::uint8_t> up_vertex_bytes;
    std::vector<std::uint8_t> constant_bytes;
    std::uint64_t constant_updates{},constant_reuses{};
    std::vector<std::uint8_t> pixel_scratch;
    std::span<std::uint8_t> scratch_pixels(std::size_t size,std::vector<std::uint8_t>& transient){
        if(size<=8u*1024*1024){if(pixel_scratch.size()<size)pixel_scratch.resize(size);return {pixel_scratch.data(),size};}
        transient.resize(size);return transient;
    }
    std::vector<DecodedVertex> decoded_vertices;
    std::uint64_t submitted_draws{},batched_guest_draws{},submitted_vertices{};
    std::uint64_t config_version{1},texture_version{1},applied_config_version{},applied_texture_version{};
    struct StateWords {std::array<U32,256> render{};std::array<std::array<U32,33>,8> stage{};std::array<std::array<U32,14>,16> sampler{};} baseline_words;
    std::array<Res,16> baseline_textures{};std::uint64_t word_differences{},texture_differences{};
    std::string driver_vendor,driver_renderer,driver_version;
    std::map<std::string,std::uint64_t> calls;
    std::uint64_t started_ms{milliseconds()};
    std::map<std::uint64_t,std::uint64_t> frame_times_ms;
    using FrameTrace=wa2::graphics::FrameMetrics;
    FrameTrace frame_trace;
    struct CopySample {std::uint64_t calls{},pixels{},total_ns{},readback_ns{};};
    std::map<std::string,CopySample> copy_samples;
    CopySample copy_total;
    std::uint64_t source_bytes_before_crop{},source_bytes_staged{},direct_source_copies{},source_staging_bytes_avoided{};
    struct CopyTimer {
        CopySample* sample{};CopySample* total{};std::chrono::steady_clock::time_point start{};
        CopyTimer(CopySample* value,CopySample* overall):sample(value),total(overall),start(std::chrono::steady_clock::now()){}
        ~CopyTimer(){const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();total->total_ns+=ns;if(sample)sample->total_ns+=ns;}
    };
    RECT last_blit_source{},last_blit_destination{};U32 last_source_width{},last_source_height{},last_destination_width{},last_destination_height{};
    std::map<std::pair<MOJOSHADER_glShader*,MOJOSHADER_glShader*>,Program> programs;
    [[noreturn]]void stop(const std::string& s){throw GuestFault(FaultKind::unsupported,p.cpu.eip,"SDL2/OpenGL: "+s);}
    void enter(bool flush_pending=true,bool needs_context=true){
        if(stopped || std::this_thread::get_id()!=thread)stop("foreign thread or stopped backend");
        if(context && needs_context){
            if(SDL_GL_GetCurrentContext()!=context || SDL_GL_GetCurrentWindow()!=window){
                if(SDL_GL_MakeCurrent(window,context))stop(SDL_GetError());++context_rebinds;
            }else ++context_reuses;
            MOJOSHADER_glMakeContextCurrent(mojo);
        }else if(context)++context_free_calls;
        if(flush_pending){flush();applied.reset();}
    }
    Object& object(U32 at){auto it=objects.find(at);if(it==objects.end() || !it->second.refs)stop("stale guest COM object");return it->second;}
    Res get(U32 at,Kind kind,bool null_ok=false){if(!at && null_ok)return {};auto r=object(at).resource;if(r->kind!=kind)stop("wrong guest interface");return r;}
    template<class T>std::vector<T> array(U32 at,std::size_t count){if(count>MaxBytes/sizeof(T))stop("staging limit");std::vector<T> v(count);if(count)p.memory.copy_out(at,std::span(reinterpret_cast<std::uint8_t*>(v.data()),count*sizeof(T)));return v;}
    template<class T>U32 output(U32 at,const T& v){write(p,at,v);return 0;}
    U32 out_res(U32 at,const Res& r){p.memory.check(at,4,Memory::Write);p.memory.store(at,r?wrap(r):0,32);return 0;}
    Res make(Kind kind){auto r=std::make_shared<Resource>();r->kind=kind;r->device=device_resource;resources.add(r);return r;}
    static std::string method_name(Kind kind,const char* method){return "COM."+std::to_string(unsigned(kind))+"."+method;}
    U32 vtable(Kind kind){if(auto it=vtables.find(kind);it!=vtables.end())return it->second;const auto table=methods(kind);auto at=p.allocate_bytes(table.size()*4);
        for(unsigned slot=0;slot<table.size();++slot)p.memory.store(at+slot*4,p.resolve("wa2.d3d9",method_name(kind,table[slot].name)),32);
        p.memory.protect(at,4096,Memory::Read);vtables.emplace(kind,at);return at;}
    U32 wrap(const Res& r){
        if(r->interface_handle){auto& o=object(r->interface_handle);if(o.resource!=r)stop("native interface identity mismatch");++o.refs;return r->interface_handle;}
        if(objects.size()>=8192)stop("live COM object limit");
        auto table=vtable(r->kind),at=handles.acquire(table);
        try{objects.emplace(at,Object{r,1,{}});}catch(...){handles.release(at);throw;}
        r->interface_handle=at;++created;
        return at;
    }
    static unsigned bpp(D3DFORMAT f){switch(f){case D3DFMT_A8R8G8B8:case D3DFMT_X8R8G8B8:case D3DFMT_A8B8G8R8:case D3DFMT_X8B8G8R8:return 4;case D3DFMT_R8G8B8:return 3;case D3DFMT_R5G6B5:case D3DFMT_X1R5G5B5:case D3DFMT_A1R5G5B5:case D3DFMT_A4R4G4B4:case D3DFMT_A8L8:return 2;case D3DFMT_A8:case D3DFMT_L8:return 1;default:return 0;}}
    static bool depth_format(D3DFORMAT f){return f==D3DFMT_D16 || f==D3DFMT_D24S8 || f==D3DFMT_D24X8;}
    static bool alpha_format(D3DFORMAT f){return f==D3DFMT_A8R8G8B8 || f==D3DFMT_A8B8G8R8 || f==D3DFMT_A1R5G5B5 || f==D3DFMT_A4R4G4B4 || f==D3DFMT_A8 || f==D3DFMT_A8L8;}
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
    std::shared_ptr<Texture> new_texture(){auto t=std::make_shared<Texture>();gl.GenTextures(1,&t->id);const auto id=t->id;t->cleanup=[this,id]{gl.DeleteTextures(1,&id);};textures_alive.add(t);return t;}
    std::shared_ptr<ImageData> new_image(U32 w,U32 h,D3DFORMAT f,U32 usage,U32 pool,std::shared_ptr<Texture> t={},U32 level=0){
        applied.reset();
        const auto bytes=std::uint64_t(w)*h*(depth_format(f)?4:bpp(f));if(!w || !h || w>U32(max_texture) || h>U32(max_texture) || !bytes || bytes>MaxBytes)stop("unsupported image size/format "+std::to_string(U32(f)));
        auto i=std::make_shared<ImageData>();i->width=w;i->height=h;i->format=f;i->usage=usage;i->pool=pool;i->level=level;i->bytes.resize(std::size_t(bytes));i->texture=t?t:new_texture();
        gl.ActiveTexture(GL_TEXTURE0);gl.BindTexture(GL_TEXTURE_2D,i->texture->id);
        if(depth_format(f))gl.TexImage2D(GL_TEXTURE_2D,level,GL_DEPTH24_STENCIL8,w,h,0,GL_DEPTH_STENCIL,GL_UNSIGNED_INT_24_8,nullptr);
        else gl.TexImage2D(GL_TEXTURE_2D,level,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);i->texture->parameters.reset();images.add(i);return i;
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
        if(bgra || !rgba32 || i->format==D3DFMT_X8B8G8R8){auto pixels=scratch_pixels(std::size_t(i->width)*i->height*4,rgba);
            if(bgra || rgba32){const auto order=bgra?(i->format==D3DFMT_X8R8G8B8?wa2::image::PixelOrder::bgrx8:wa2::image::PixelOrder::bgra8):wa2::image::PixelOrder::rgbx8;
                wa2::image::transfer_32(pixels,std::size_t(i->width)*4,wa2::image::PixelOrder::rgba8,i->bytes,std::size_t(i->width)*4,order,i->width,i->height);}
            else if(i->format==D3DFMT_R8G8B8 && pixels.size()<=64u*1024*1024){
                wa2::image::transfer_bgr24(pixels,std::size_t(i->width)*4,wa2::image::PixelOrder::rgba8,i->bytes,std::size_t(i->width)*3,i->width,i->height);}
            else{const auto size=bpp(i->format);for(std::size_t n=0;n<pixels.size()/4;++n){const auto c=decode(data+n*size,i->format);std::copy(c.begin(),c.end(),pixels.data()+n*4);}}
            data=pixels.data();}
        GLint active=0,binding=0;gl.GetIntegerv(GL_ACTIVE_TEXTURE,&active);gl.ActiveTexture(GL_TEXTURE0);gl.GetIntegerv(GL_TEXTURE_BINDING_2D,&binding);gl.BindTexture(GL_TEXTURE_2D,i->texture->id);gl.PixelStorei(GL_UNPACK_ALIGNMENT,1);
        gl.TexSubImage2D(GL_TEXTURE_2D,i->level,0,0,i->width,i->height,GL_RGBA,GL_UNSIGNED_BYTE,data);
        gl.BindTexture(GL_TEXTURE_2D,GLuint(binding));gl.ActiveTexture(GLenum(active));i->cpu_dirty=false;
    }
    void download(const std::shared_ptr<ImageData>& i){if(!i->gpu_dirty)return;fbo(i);gl.PixelStorei(GL_PACK_ALIGNMENT,1);
        const bool bgra=i->format==D3DFMT_A8R8G8B8 || i->format==D3DFMT_X8R8G8B8,rgba32=i->format==D3DFMT_A8B8G8R8 || i->format==D3DFMT_X8B8G8R8;
        std::vector<std::uint8_t> transient;auto pixels=rgba32?std::span(i->bytes):scratch_pixels(std::size_t(i->width)*i->height*4,transient);
        gl.ReadPixels(0,0,i->width,i->height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        if(bgra || rgba32){const auto order=bgra?(i->format==D3DFMT_X8R8G8B8?wa2::image::PixelOrder::bgrx8:wa2::image::PixelOrder::bgra8):
                (i->format==D3DFMT_X8B8G8R8?wa2::image::PixelOrder::rgbx8:wa2::image::PixelOrder::rgba8);
            if(order!=wa2::image::PixelOrder::rgba8 || pixels.data()!=i->bytes.data())wa2::image::transfer_32(i->bytes,std::size_t(i->width)*4,order,pixels,std::size_t(i->width)*4,wa2::image::PixelOrder::rgba8,i->width,i->height);}
        else{const auto size=bpp(i->format);for(std::size_t n=0;n<pixels.size()/4;++n)encode(i->bytes.data()+n*size,i->format,{pixels[n*4],pixels[n*4+1],pixels[n*4+2],pixels[n*4+3]});}
        i->gpu_dirty=false;i->cpu_dirty=false;}
    std::shared_ptr<ImageData> image(const Res& r,U32 level=0){if(!r || (r->kind!=Kind::texture && r->kind!=Kind::surface) || level>=r->levels.size())stop("invalid image level");return r->levels[level];}
    Res surface(const std::shared_ptr<ImageData>& i,Res parent={}){if(auto r=i->surface.lock())return r;auto r=make(Kind::surface);r->levels={i};r->container=parent;i->surface=r;return r;}
    D3DSURFACE_DESC description(const std::shared_ptr<ImageData>& i){return {i->format,D3DRTYPE_SURFACE,i->usage,D3DPOOL(i->pool),D3DMULTISAMPLE_NONE,0,i->width,i->height};}
    Res texture(U32 w,U32 h,U32 levels,U32 usage,D3DFORMAT format,U32 pool){if(!context)stop("texture before device");if(!bpp(format))stop("unsupported texture format "+std::to_string(U32(format)));auto r=make(Kind::texture);r->usage=usage;r->pool=pool;r->format=format;auto t=new_texture();U32 max=1;for(U32 n=std::max(w,h);n>1;n>>=1)++max;if(!levels)levels=max;if(levels>max)stop("invalid mip count");for(U32 n=0;n<levels;++n)r->levels.push_back(new_image(std::max(1u,w>>n),std::max(1u,h>>n),format,usage,pool,t,n));gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,GLint(levels-1));++textures;return r;}
    RECT rectangle(U32 at,const std::shared_ptr<ImageData>& i){return at?read<RECT>(p,at):RECT{0,0,LONG(i->width),LONG(i->height)};}
    static bool valid(const RECT& r,U32 w,U32 h){return r.left>=0 && r.top>=0 && r.right>r.left && r.bottom>r.top && U32(r.right)<=w && U32(r.bottom)<=h;}
    U32 lock_rect(Object& o,U32 level,U32 at,U32 area,U32 flags){p.memory.check(at,8,Memory::Write);if(o.locks.contains(level))return U32(D3DERR_INVALIDCALL);auto i=image(o.resource,level);const auto r=rectangle(area,i);if(!valid(r,i->width,i->height) || !bpp(i->format))return U32(D3DERR_INVALIDCALL);download(i);const auto row=U32(r.right-r.left)*bpp(i->format),pitch=(row+3u)&~3u,rows=U32(r.bottom-r.top);auto guest=p.allocate_bytes(std::size_t(pitch)*rows);if(!(flags&D3DLOCK_DISCARD))for(U32 y=0;y<rows;++y)p.memory.copy_in(guest+y*pitch,std::span(i->bytes.data()+(std::size_t(y+r.top)*i->width+r.left)*bpp(i->format),row));const bool ro=(flags&D3DLOCK_READONLY)!=0;if(ro)p.memory.protect(guest,(std::size_t(pitch)*rows+4095)&~std::size_t(4095),Memory::Read);o.locks.emplace(level,Lock{guest,pitch,rows,row,0,r,ro});write(p,at,std::array<U32,2>{pitch,guest});return 0;}
    U32 unlock_rect(Object& o,U32 level){auto it=o.locks.find(level);if(it==o.locks.end())return U32(D3DERR_INVALIDCALL);const auto l=it->second;auto i=image(o.resource,level);if(!l.readonly){
        const auto stride=std::size_t(i->width)*bpp(i->format),offset=(std::size_t(l.area.top)*i->width+l.area.left)*bpp(i->format);
        wa2::graphics::read_source_pixel_rows(p.memory,l.guest,l.pitch,std::span(i->bytes).subspan(offset),stride,l.row_bytes,l.rows);
        i->cpu_dirty=true;i->gpu_dirty=false;++uploads;}p.memory.release(l.guest);o.locks.erase(it);return 0;}
    U32 lock_buffer(Object& o,Args a){p.memory.check(a[3],4,Memory::Write);if(!o.locks.empty())return U32(D3DERR_INVALIDCALL);auto& bytes=o.resource->bytes;if(a[1]>=bytes.size() || (a[2] && a[2]>bytes.size()-a[1]))return U32(D3DERR_INVALIDCALL);U32 n=a[2]?a[2]:U32(bytes.size())-a[1];const auto guest=p.allocate_bytes(n);if(!(a[4]&D3DLOCK_DISCARD))p.memory.copy_in(guest,std::span(bytes.data()+a[1],n));const bool ro=(a[4]&D3DLOCK_READONLY)!=0;if(ro)p.memory.protect(guest,(std::size_t(n)+4095)&~std::size_t(4095),Memory::Read);o.locks.emplace(0,Lock{guest,n,1,n,a[1],{},ro});p.memory.store(a[3],guest,32);return 0;}
    U32 unlock_buffer(Object& o){auto it=o.locks.find(0);if(it==o.locks.end())return U32(D3DERR_INVALIDCALL);const auto l=it->second;if(!l.readonly){p.memory.copy_out(l.guest,std::span(o.resource->bytes.data()+l.offset,l.row_bytes));++uploads;}p.memory.release(l.guest);o.locks.erase(it);return 0;}
    U32 method(Object&,unsigned,Args);U32 device(unsigned,Args);U32 init(U32,U32);void defaults();

    void set_texture(unsigned unit,Res texture,U32 token){
        if(state.textures[unit]!=texture){texture_differences-=state.textures[unit]!=baseline_textures[unit];texture_differences+=texture!=baseline_textures[unit];++texture_version;}
        state.textures[unit]=std::move(texture);
    }
    void reset_baseline(){baseline_words={state.render,state.stage,state.sampler};baseline_textures=state.textures;word_differences=texture_differences=0;}
    void recount_differences(){word_differences=texture_differences=0;for(unsigned j=0;j<256;++j)word_differences+=state.render[j]!=baseline_words.render[j];
        for(unsigned unit=0;unit<8;++unit)for(unsigned j=0;j<33;++j)word_differences+=state.stage[unit][j]!=baseline_words.stage[unit][j];
        for(unsigned unit=0;unit<16;++unit){for(unsigned j=0;j<14;++j)word_differences+=state.sampler[unit][j]!=baseline_words.sampler[unit][j];texture_differences+=state.textures[unit]!=baseline_textures[unit];}}
    void set_word(U32& destination,U32 value,U32 baseline){if(destination!=value){word_differences-=destination!=baseline;word_differences+=value!=baseline;destination=value;}}
    void apply();void flush();void bind_sampler(unsigned,const Res&,const std::array<U32,14>* =nullptr);
    void texture_mode(bool enabled){if(texture_batch_mode!=enabled){flush();texture_batch_mode=enabled;applied.reset();}}
    bool batch_textures(std::span<const wa2::graphics::ScreenVertex>,U32,U32,std::span<const U32>);
    U32 draw(U32,U32,std::span<const std::uint8_t>,U32,std::span<const U32> indices = {});
    U32 present(Args);D3DCAPS9 caps();
    U32 copy(const std::shared_ptr<ImageData>&,RECT,const std::uint8_t*,U32,D3DFORMAT,RECT,U32,U32);
    std::vector<DWORD> shader(U32);U32 compile(U32,U32,Kind);
public:
    explicit Graphics(Process& q,std::shared_ptr<wa2::graphics::SurfaceHost> h):p(q),handles(q),host(std::move(h)){if(!host)throw std::invalid_argument("graphics surface host required");}~Graphics(){shutdown();}
    void install();void shutdown() noexcept;
    void movie_frame(const wa2::movie::VideoFrame&,const wa2::graphics::MovieView&)override;
    void restore_game()override;
    std::string report() const {
        std::ostringstream out;const auto array=[&](const auto& values){out<<'[';bool first=true;for(const auto value:values){if(!first)out<<',';first=false;out<<value;}out<<']';};
        out<<"{\"backend\":\"shared-sdl-gl\",\"presents\":"<<presents<<",\"draws\":"<<draws<<",\"submitted_draws\":"<<submitted_draws<<",\"interface_handles\":"<<handles.report()<<",\"live_objects\":"<<objects.size()<<",\"shader_programs\":"<<programs.size()<<",\"backbuffer_size\":["<<presentation[0]<<','<<presentation[1]<<"],\"drawable_size\":";array(last_drawable_size);
        out<<",\"present_source\":";array(last_present_source);out<<",\"present_destination\":";array(last_present_destination);
        out<<",\"movie\":{\"frames\":"<<movie_frames<<",\"client_size\":";array(last_movie_client);out<<",\"frame_size\":";array(last_movie_size);
        out<<",\"source_crop_xywh\":";array(last_movie_source);out<<",\"requested_client_xywh\":";array(last_movie_requested);out<<",\"drawable_xywh\":";array(last_movie_destination);
        out<<"},\"state_applies\":"<<state_applies<<",\"state_cache_hits\":"<<state_cache_hits<<",\"texture_only_applies\":"<<texture_only_applies
            <<",\"compact_screen_draws\":"<<screen_draws<<",\"compact_screen_unique_vertices\":"<<screen_unique_vertices<<",\"vertex_upload_bytes\":"<<vertex_upload_bytes
            <<",\"index_upload_bytes\":"<<index_upload_bytes
            <<",\"multi_texture_batch\":{\"available\":"<<(texture_batch_available?"true":"false")<<",\"slots\":"<<wa2::graphics::TextureBatchSlots
            <<",\"guest_draws\":"<<texture_batch_draws<<",\"driver_submissions\":"<<texture_batch_submissions<<",\"failure\":"<<quote(texture_batch_failure)<<'}'
            <<",\"context_rebinds\":"<<context_rebinds<<",\"context_reuses\":"<<context_reuses<<",\"context_free_calls\":"<<context_free_calls
            <<",\"constant_updates\":"<<constant_updates<<",\"constant_reuses\":"<<constant_reuses<<",\"sampler_parameter_calls\":"<<sampler_parameter_calls<<",\"sampler_parameter_skips\":"<<sampler_parameter_skips
            <<",\"framebuffer_checks\":"<<framebuffer_checks<<",\"swap_interval\":{\"requested\":"<<requested_swap_interval<<",\"actual\":"<<actual_swap_interval<<",\"result\":"<<swap_interval_result
            <<"},\"driver\":{\"vendor\":"<<quote(driver_vendor)<<",\"renderer\":"<<quote(driver_renderer)<<",\"version\":"<<quote(driver_version)<<"},\"calls\":{";
        bool first=true;for(const auto& [name,count]:calls){if(!first)out<<',';first=false;out<<quote(name)<<':'<<count;}
        out<<"},\"screen_capture\":false}";return out.str();
    }

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
MOJOSHADER_glShader* Graphics::assemble_shader(const std::string& source){
    const auto* code=MOJOSHADER_assemble("fixed-pipeline",source.data(),unsigned(source.size()),
        nullptr,0,nullptr,0,nullptr,0,nullptr,nullptr,nullptr,nullptr,nullptr);
    if(!code || code==&WA2_assembler_MOJOSHADER_out_of_mem_data)stop("fixed shader allocation failed");
    struct Owner {const MOJOSHADER_parseData* code;~Owner(){MOJOSHADER_freeParseData(code);}} owner{code};
    if(code->error_count)stop(std::string("fixed shader assembly: ")+code->errors[0].error);
    auto* shader=MOJOSHADER_glCompileShader(reinterpret_cast<const unsigned char*>(code->output),
        unsigned(code->output_len),nullptr,0,nullptr,0);
    if(!shader)stop(std::string("fixed shader compilation: ")+MOJOSHADER_glGetError()+" "+transform_error);
    return shader;
}
MOJOSHADER_glShader* Graphics::fixed_pixel(bool batched){
    wa2::graphics::FixedPipeline pipeline{};std::array<U32,65> key{};key[64]=batched?wa2::graphics::TextureBatchSlots:1;
    for(unsigned n=0;n<8;++n){const auto& t=state.stage[n];pipeline.stages[n]={t[D3DTSS_COLOROP],t[D3DTSS_COLORARG1],
        t[D3DTSS_COLORARG2],t[D3DTSS_ALPHAOP],t[D3DTSS_ALPHAARG1],t[D3DTSS_ALPHAARG2],
        t[D3DTSS_TEXTURETRANSFORMFLAGS],bool(state.textures[n])};
        const auto& s=pipeline.stages[n];const std::array<U32,8> words{s.colour_op,s.colour_arg1,s.colour_arg2,s.alpha_op,s.alpha_arg1,s.alpha_arg2,s.transform_flags,U32(s.texture)};std::copy(words.begin(),words.end(),key.begin()+n*8);}
    if(const auto found=fixed_pixels.find(key);found!=fixed_pixels.end())return found->second;
    if(fixed_pixels.size()>=256){
        // No queued draw survives a pipeline change. Bound the native shader
        // cache during long fast-forward sessions without retaining old COMs.
        MOJOSHADER_glBindProgram(nullptr);
        for(auto& [key,program]:programs)MOJOSHADER_glDeleteProgram(program.shader);programs.clear();
        for(auto& [key,shader]:fixed_pixels)MOJOSHADER_glDeleteShader(shader);fixed_pixels.clear();
    }
    auto* shader=assemble_shader(wa2::graphics::fixed_pixel_assembly(pipeline,key[64]));fixed_pixels.emplace(key,shader);return shader;
}
void Graphics::prepare_program(){
    const auto vs=state.vertex_shader?state.vertex_shader->shader:default_vertex;
    const auto ps=state.pixel_shader?state.pixel_shader->shader:fixed_pixel(texture_batch_mode);
    const auto key=std::pair{vs,ps};auto found=programs.find(key);
    if(found==programs.end()){
        if(programs.size()>=512){MOJOSHADER_glBindProgram(nullptr);for(auto& [key,value]:programs)MOJOSHADER_glDeleteProgram(value.shader);programs.clear();}
        auto* shader=MOJOSHADER_glLinkProgram(vs,ps);if(!shader)stop(std::string("shader link: ")+MOJOSHADER_glGetError());
        Program program{shader};const auto native=wa2_mojo_program_handle(shader);
        program.alpha_mode=gl.GetUniformLocation(native,"wa2_alpha_mode");
        program.alpha_reference=gl.GetUniformLocation(native,"wa2_alpha_reference");
        found=programs.emplace(key,program).first;
    }
    const auto& program=found->second;MOJOSHADER_glBindProgram(program.shader);
    MOJOSHADER_glSetVertexShaderUniformF(0,state.vertex_f[0].data(),256);
    MOJOSHADER_glSetPixelShaderUniformF(0,state.pixel_f[0].data(),256);
    MOJOSHADER_glSetVertexShaderUniformI(0,state.vertex_i[0].data(),16);
    MOJOSHADER_glSetPixelShaderUniformI(0,state.pixel_i[0].data(),16);
    MOJOSHADER_glSetVertexShaderUniformB(0,state.vertex_b.data(),16);
    MOJOSHADER_glSetPixelShaderUniformB(0,state.pixel_b.data(),16);
    if(!state.pixel_shader){const auto c=state.render[D3DRS_TEXTUREFACTOR];const std::array<float,4> factor{
        float((c>>16)&255)/255,float((c>>8)&255)/255,float(c&255)/255,float(c>>24)/255};
        MOJOSHADER_glSetPixelShaderUniformF(0,factor.data(),1);}
    if(program.alpha_mode>=0)gl.Uniform1i(program.alpha_mode,state.render[D3DRS_ALPHATESTENABLE]?int(state.render[D3DRS_ALPHAFUNC]):0);
    if(program.alpha_reference>=0)gl.Uniform1f(program.alpha_reference,float(state.render[D3DRS_ALPHAREF]&255)/255);
}
DecodedVertex Graphics::fixed_vertex(const DecodedVertex& source){
    auto out=source;
    const auto transform=[](std::array<float,4> v,const D3DMATRIX& m){std::array<float,4> result{};
        for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k)result[j]+=v[k]*m.m[k][j];return result;};
    bool screen=(state.fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW;
    if(state.declaration)for(const auto& e:state.declaration->elements)screen|=e.Usage==D3DDECLUSAGE_POSITIONT;
    if(screen)out.pos=wa2::graphics::screen_position(source.pos,state.viewport);
    else{const auto position=transform(transform(transform(source.pos,state.transforms[D3DTS_WORLD]),state.transforms[D3DTS_VIEW]),state.transforms[D3DTS_PROJECTION]);
        out.pos={position[0],-position[1],2*position[2]-position[3],position[3]};}
    for(unsigned unit=0;unit<8;++unit){const auto& stage=state.stage[unit];const auto index=stage[D3DTSS_TEXCOORDINDEX];
        if((index&0xffff)>=8 || (index>>16))stop("unsupported generated texture coordinates");
        out.uv[unit]=source.uv[index];
        const auto flags=stage[D3DTSS_TEXTURETRANSFORMFLAGS];if(flags){
            if((flags&255)<1 || (flags&255)>4 || (flags&~0x1ffu))stop("invalid texture transform flags");
            out.uv[unit]=transform(out.uv[unit],state.transforms[D3DTS_TEXTURE0+unit]);
        }
    }
    return out;
}
void Graphics::submit(GLenum primitive,std::span<const DecodedVertex> vertices,std::span<const U32> indices){
    gl.BindBuffer(GL_ARRAY_BUFFER,vertex_buffer);
    gl.BufferData(GL_ARRAY_BUFFER,GLsizeiptr(vertices.size_bytes()),vertices.data(),GL_STREAM_DRAW);
    const unsigned stride=sizeof(DecodedVertex);
    const auto attribute=[&](MOJOSHADER_usage usage,int index,unsigned count,std::size_t offset){
        MOJOSHADER_glSetVertexAttribute(usage,index,count,MOJOSHADER_ATTRIBUTE_FLOAT,0,stride,reinterpret_cast<const void*>(offset));};
    attribute(MOJOSHADER_USAGE_POSITION,0,4,offsetof(DecodedVertex,pos));
    attribute(MOJOSHADER_USAGE_NORMAL,0,3,offsetof(DecodedVertex,normal));
    attribute(MOJOSHADER_USAGE_COLOR,0,4,offsetof(DecodedVertex,color));
    attribute(MOJOSHADER_USAGE_COLOR,1,4,offsetof(DecodedVertex,specular));
    for(unsigned n=0;n<8;++n)attribute(MOJOSHADER_USAGE_TEXCOORD,n,4,offsetof(DecodedVertex,uv)+n*sizeof(std::array<float,4>));
    MOJOSHADER_glProgramReady();MOJOSHADER_glProgramViewportInfo(state.viewport.Width,state.viewport.Height,image(backbuffer)->width,image(backbuffer)->height,1);
    if(indices.empty())gl.DrawArrays(primitive,0,GLsizei(vertices.size()));
    else{gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER,index_buffer);gl.BufferData(GL_ELEMENT_ARRAY_BUFFER,GLsizeiptr(indices.size_bytes()),indices.data(),GL_STREAM_DRAW);
        gl.DrawElements(primitive,GLsizei(indices.size()),GL_UNSIGNED_INT,nullptr);}
    ++submitted_draws;submitted_vertices+=indices.empty()?vertices.size():indices.size();
}
U32 Graphics::init(U32 pp,U32 focus){
    if(context)stop("multiple devices are outside this target profile");presentation=read<std::array<U32,14>>(p,pp);focus_window=focus;
    const auto extent=host->backbuffer_size(presentation[7]?presentation[7]:focus,presentation[0],presentation[1],presentation[8]!=0);
    if(!extent)return U32(D3DERR_INVALIDCALL);presentation[0]=(*extent)[0];presentation[1]=(*extent)[1];
    window=host->acquire(presentation[7]?presentation[7]:focus,presentation[0],presentation[1],presentation[8]!=0);
    if(!window)stop("device requires an SDL OpenGL window");
    context=SDL_GL_CreateContext(window);if(!context)stop(SDL_GetError());
    gl.init(host->embedded_gl());gl.GetIntegerv(GL_MAX_TEXTURE_SIZE,&max_texture);
    GLint texture_units{};gl.GetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&texture_units);
    bool gradients=gl.embedded;
    if(!gradients){const auto* extensions=gl.GetString(GL_EXTENSIONS);if(extensions){
        const std::string list=" "+std::string(reinterpret_cast<const char*>(extensions))+" ";
        gradients=list.find(" GL_ARB_shader_texture_lod ")!=list.npos;
    }}
    texture_batch_available=gradients && texture_units>=GLint(wa2::graphics::TextureBatchSlots);
    const auto driver_string=[&](GLenum name){const auto text=gl.GetString(name);return text?std::string(reinterpret_cast<const char*>(text)):std::string();};
    driver_vendor=driver_string(GL_VENDOR);driver_renderer=driver_string(GL_RENDERER);driver_version=driver_string(GL_VERSION);
    mojo=MOJOSHADER_glCreateContext(host->embedded_gl()?MOJOSHADER_PROFILE_GLSLES3:MOJOSHADER_PROFILE_GLSL120,
        [](const char* name,void*)->void*{return SDL_GL_GetProcAddress(name);},nullptr,nullptr,nullptr,nullptr);
    if(!mojo)stop(MOJOSHADER_glGetError());MOJOSHADER_glMakeContextCurrent(mojo);
    wa2_mojo_set_source_transform([](const MOJOSHADER_parseData* data,int* length,void* userdata)->const char*{
        auto& self=*static_cast<Graphics*>(userdata);
        try{self.transformed_source=std::string(data->output,std::size_t(data->output_len));
            if(data->shader_type==MOJOSHADER_TYPE_PIXEL)self.transformed_source=wa2::graphics::alpha_test_source(self.transformed_source);
            *length=int(self.transformed_source.size());return self.transformed_source.c_str();
        }catch(const std::exception& error){self.transform_error=error.what();*length=-1;return nullptr;}
    },this);
    gl.GenBuffers(1,&vertex_buffer);gl.GenBuffers(1,&index_buffer);
    default_vertex=assemble_shader(wa2::graphics::fixed_vertex_assembly());
    requested_swap_interval=presentation[13]==D3DPRESENT_INTERVAL_IMMEDIATE?0:1;
    swap_interval_result=SDL_GL_SetSwapInterval(requested_swap_interval);actual_swap_interval=SDL_GL_GetSwapInterval();
    if(!presentation[2])presentation[2]=D3DFMT_X8R8G8B8;
    device_resource=make(Kind::device);backbuffer=surface(new_image(presentation[0],presentation[1],D3DFORMAT(presentation[2]),D3DUSAGE_RENDERTARGET,D3DPOOL_DEFAULT));defaults();
    if(presentation[9])state.depth=surface(new_image(presentation[0],presentation[1],D3DFORMAT(presentation[10]),D3DUSAGE_DEPTHSTENCIL,D3DPOOL_DEFAULT));
    write(p,pp,presentation);++devices;return 0;
}
D3DCAPS9 Graphics::caps(){
    D3DCAPS9 c{};c.DeviceType=D3DDEVTYPE_HAL;c.Caps=D3DCAPS_READ_SCANLINE;c.PresentationIntervals=D3DPRESENT_INTERVAL_ONE|D3DPRESENT_INTERVAL_IMMEDIATE;
    c.DevCaps=D3DDEVCAPS_DRAWPRIMTLVERTEX|D3DDEVCAPS_EXECUTESYSTEMMEMORY|D3DDEVCAPS_TEXTURESYSTEMMEMORY|D3DDEVCAPS_TEXTUREVIDEOMEMORY|D3DDEVCAPS_DRAWPRIMITIVES2|D3DDEVCAPS_DRAWPRIMITIVES2EX|D3DDEVCAPS_HWRASTERIZATION;
    c.PrimitiveMiscCaps=D3DPMISCCAPS_CULLNONE|D3DPMISCCAPS_CULLCW|D3DPMISCCAPS_CULLCCW|D3DPMISCCAPS_COLORWRITEENABLE|D3DPMISCCAPS_BLENDOP|D3DPMISCCAPS_SEPARATEALPHABLEND;
    c.RasterCaps=D3DPRASTERCAPS_SCISSORTEST;c.ZCmpCaps=c.AlphaCmpCaps=255;c.SrcBlendCaps=c.DestBlendCaps=0x3fff;
    c.ShadeCaps=D3DPSHADECAPS_COLORGOURAUDRGB|D3DPSHADECAPS_ALPHAGOURAUDBLEND;
    c.TextureCaps=D3DPTEXTURECAPS_ALPHA|D3DPTEXTURECAPS_PERSPECTIVE;c.TextureFilterCaps=D3DPTFILTERCAPS_MINFPOINT|D3DPTFILTERCAPS_MINFLINEAR|D3DPTFILTERCAPS_MAGFPOINT|D3DPTFILTERCAPS_MAGFLINEAR;
    c.TextureAddressCaps=D3DPTADDRESSCAPS_WRAP|D3DPTADDRESSCAPS_MIRROR|D3DPTADDRESSCAPS_CLAMP|D3DPTADDRESSCAPS_INDEPENDENTUV;
    if(!host->embedded_gl())c.TextureAddressCaps|=D3DPTADDRESSCAPS_BORDER;
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
    const auto pixel_order=[](D3DFORMAT format){return format==D3DFMT_A8R8G8B8?wa2::image::PixelOrder::bgra8:
        format==D3DFMT_X8R8G8B8?wa2::image::PixelOrder::bgrx8:format==D3DFMT_X8B8G8R8?wa2::image::PixelOrder::rgbx8:wa2::image::PixelOrder::rgba8;};
    if(sw==dw && sh==dh && !key && dst32 && (src32 || f==D3DFMT_R8G8B8) &&
       std::uint64_t(pitch)*sr.bottom<=64u*1024*1024 && std::uint64_t(d->width)*4*dh<=64u*1024*1024){
        const auto source_bytes=src32?4u:3u;const auto input=source+std::size_t(sr.top)*pitch+std::size_t(sr.left)*source_bytes;
        const auto begin=reinterpret_cast<std::uintptr_t>(source),output_begin=reinterpret_cast<std::uintptr_t>(d->bytes.data());
        const bool alias=begin<output_begin+d->bytes.size() && output_begin<begin+std::size_t(pitch)*sr.bottom;
        if(!alias){auto output=std::span(d->bytes).subspan((std::size_t(dr.top)*d->width+dr.left)*4);
            const auto bytes=std::size_t(sh-1)*pitch+std::size_t(sw)*source_bytes;
            if(src32)wa2::image::transfer_32(output,std::size_t(d->width)*4,pixel_order(d->format),{input,bytes},pitch,pixel_order(f),sw,sh);
            else wa2::image::transfer_bgr24(output,std::size_t(d->width)*4,pixel_order(d->format),{input,bytes},pitch,sw,sh);
            d->cpu_dirty=true;d->gpu_dirty=false;++uploads;return 0;}
    }
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
    if(context){if(SDL_GL_MakeCurrent(window,context))return;MOJOSHADER_glMakeContextCurrent(mojo);
        try{flush();}catch(...){}if(mojo)MOJOSHADER_glBindProgram(nullptr);}
    applied.reset();
    for(auto& [at,o]:objects){for(const auto& [level,l]:o.locks)try{p.memory.release(l.guest);}catch(...){}
        if(o.resource->data)try{p.memory.release(o.resource->data);o.resource->data=0;}catch(...){}}
    for(auto& [key,program]:programs)MOJOSHADER_glDeleteProgram(program.shader);programs.clear();
    if(default_vertex){MOJOSHADER_glDeleteShader(default_vertex);default_vertex=nullptr;}
    if(movie_shader){MOJOSHADER_glDeleteShader(movie_shader);movie_shader=nullptr;}
    if(movie_planes[0])gl.DeleteTextures(3,movie_planes.data());movie_planes={};
    for(auto& [key,shader]:fixed_pixels)MOJOSHADER_glDeleteShader(shader);fixed_pixels.clear();
    for(auto& w:resources)if(auto r=w.lock())if(r->cleanup){r->cleanup();r->cleanup={};r->shader=nullptr;}
    state=State{};recording.reset();objects.clear();handles.clear();device_resource.reset();backbuffer.reset();root.reset();
    for(auto& w:images)if(auto i=w.lock())if(i->cleanup){i->cleanup();i->cleanup={};}
    for(auto& w:textures_alive)if(auto t=w.lock())if(t->cleanup){t->cleanup();t->cleanup={};}
    if(vertex_buffer)gl.DeleteBuffers(1,&vertex_buffer);if(index_buffer)gl.DeleteBuffers(1,&index_buffer);
    if(mojo){MOJOSHADER_glDestroyContext(mojo);mojo=nullptr;}if(context){SDL_GL_DeleteContext(context);context=nullptr;}window=nullptr;
    if(initialized){SDL_QuitSubSystem(SDL_INIT_VIDEO);initialized=false;}
}
U32 Graphics::method(Object& o,unsigned slot,Args a){
    const auto r=o.resource;
    // Setters below alter only the CPU state. Leave earlier geometry queued
    // until the next draw chooses a pipeline, or an observable resource/API
    // boundary requires it. Pixel operations and EndScene always flush.
    // A borrowed render-target interface may reach zero COM references while
    // the device state still owns its backing surface. Releasing only that
    // guest handle cannot destroy pixels or invalidate queued geometry.
    const bool retained_surface=r->kind==Kind::surface && !r->data && o.locks.empty() &&
        (state.target==r || state.depth==r || (applied && (applied->target==r || applied->depth==r)));
    const bool metadata=(r->kind==Kind::texture && wa2::graphics::texture_metadata_method(slot)) ||
        (r->kind==Kind::surface && wa2::graphics::surface_metadata_method(slot));
    const bool deferred=slot==0 || slot==1 || metadata || (slot==2 && (o.refs>1 || retained_surface)) ||
        (r->kind==Kind::device && wa2::graphics::cpu_only_device_method(slot));
    if(!deferred){flush();applied.reset();}
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
    if(slot==2){if(o.refs==1 && !o.locks.empty())stop("final release of locked resource");const auto refs=--o.refs;if(!refs){if(r->data){p.memory.release(r->data);r->data=0;}r->interface_handle=0;handles.release(a[0]);objects.erase(a[0]);}return refs;}
    if(r->kind==Kind::d3d){const auto fmt=[](U32 f){return bpp(D3DFORMAT(f)) || depth_format(D3DFORMAT(f));};switch(slot){
        case 4:return U32(std::max(0,SDL_GetNumVideoDisplays()));
        case 5:{D3DADAPTER_IDENTIFIER9 v{};std::strcpy(v.Driver,"SDL2 shared GL");std::strcpy(v.Description,"SDL2 shared graphics adapter");std::strcpy(v.DeviceName,"SDL2 display");p.memory.check(a[3],1100,Memory::Write);p.memory.copy_in(a[3],std::span(reinterpret_cast<const std::uint8_t*>(&v),1100));return 0;}
        case 6:return a[1]<U32(SDL_GetNumVideoDisplays()) && fmt(a[2])?U32(std::max(0,SDL_GetNumDisplayModes(int(a[1])))):0;
        case 7:case 8:{SDL_DisplayMode m{};if((slot==7?SDL_GetDisplayMode(int(a[1]),int(a[3]),&m):SDL_GetCurrentDisplayMode(int(a[1]),&m)))return U32(D3DERR_INVALIDCALL);return output(slot==7?a[4]:a[2],D3DDISPLAYMODE{U32(m.w),U32(m.h),U32(m.refresh_rate),slot==7?D3DFORMAT(a[2]):D3DFMT_X8R8G8B8});}
        case 9:return fmt(a[3]) && fmt(a[4])?0:U32(D3DERR_NOTAVAILABLE);
        case 10:return fmt(a[6])?0:U32(D3DERR_NOTAVAILABLE);
        case 11:if(a[6])p.memory.store(a[6],a[5]==D3DMULTISAMPLE_NONE?1:0,32);return a[5]==D3DMULTISAMPLE_NONE?0:U32(D3DERR_NOTAVAILABLE);
        case 12:return depth_format(D3DFORMAT(a[5]))?0:U32(D3DERR_NOTAVAILABLE);
        case 13:return fmt(a[3]) && fmt(a[4])?0:U32(D3DERR_NOTAVAILABLE);
        case 14:return output(a[3],caps());case 15:return a[1]<U32(SDL_GetNumVideoDisplays())?0xbc000000+a[1]*16:0;
        case 16:{p.memory.check(a[6],4,Memory::Write);p.memory.store(a[6],0,32);p.memory.check(a[5],56,Memory::Read|Memory::Write);creation_flags=a[4];if(const auto hr=init(a[5],a[3]))return hr;if(!(a[4]&D3DCREATE_FPU_PRESERVE))p.cpu.fp.put16(0,std::uint16_t((p.cpu.fp.get16(0)&~0x0f00u)|0x003fu));return out_res(a[6],device_resource);}
    }}
    if(r->kind==Kind::device)return device(slot,a);
    if(slot==3 && r->kind!=Kind::buffer)return out_res(a[1],r->device.lock());
    const bool resource=r->kind==Kind::texture || r->kind==Kind::surface || r->kind==Kind::vertexbuffer || r->kind==Kind::indexbuffer;
    if(resource)switch(slot){case 7:{const auto old=r->priority;r->priority=a[1];return old;}case 8:return r->priority;case 9:if(r->kind==Kind::texture)for(const auto& i:r->levels)upload(i);return 0;case 10:return r->kind==Kind::texture?D3DRTYPE_TEXTURE:r->kind==Kind::surface?D3DRTYPE_SURFACE:r->kind==Kind::vertexbuffer?D3DRTYPE_VERTEXBUFFER:D3DRTYPE_INDEXBUFFER;}
    if(r->kind==Kind::texture)switch(slot){case 11:{const auto old=r->lod;r->lod=std::min(a[1],U32(r->levels.size()-1));return old;}case 12:return r->lod;case 13:return U32(r->levels.size());case 17:if(a[1]>=r->levels.size())return U32(D3DERR_INVALIDCALL);return output(a[2],description(image(r,a[1])));case 18:if(a[1]>=r->levels.size())return U32(D3DERR_INVALIDCALL);return out_res(a[2],surface(image(r,a[1]),r));case 19:return lock_rect(o,a[1],a[2],a[3],a[4]);case 20:return unlock_rect(o,a[1]);case 21:if(a[1])read<RECT>(p,a[1]);return 0;}
    if(r->kind==Kind::surface)switch(slot){case 11:{read<GUID>(p,a[1]);auto parent=r->container.lock();if(!parent)return U32(E_NOINTERFACE);return out_res(a[2],parent);}case 12:return output(a[1],description(image(r)));case 13:return lock_rect(o,0,a[1],a[2],a[3]);case 14:return unlock_rect(o,0);}
    if(r->kind==Kind::vertexbuffer || r->kind==Kind::indexbuffer){if(slot==11)return lock_buffer(o,a);if(slot==12)return unlock_buffer(o);if(slot==13){if(r->kind==Kind::vertexbuffer){D3DVERTEXBUFFER_DESC d{};d.Type=D3DRTYPE_VERTEXBUFFER;d.Format=D3DFMT_VERTEXDATA;d.Usage=r->usage;d.Pool=D3DPOOL(r->pool);d.Size=U32(r->bytes.size());d.FVF=r->fvf;return output(a[1],d);}D3DINDEXBUFFER_DESC d{};d.Type=D3DRTYPE_INDEXBUFFER;d.Format=D3DFORMAT(r->format);d.Usage=r->usage;d.Pool=D3DPOOL(r->pool);d.Size=U32(r->bytes.size());return output(a[1],d);}}
    if(r->kind==Kind::stateblock){if(slot==4){r->saved=std::make_shared<State>(state);return 0;}if(slot==5){const auto target=state.target,depth=state.depth;const bool scene=state.scene;state=*r->saved;state.target=target;state.depth=depth;state.scene=scene;++config_version;++texture_version;recount_differences();return 0;}}
    if(r->kind==Kind::buffer){if(slot==4)return U32(r->bytes.size());if(slot==3){if(!r->data){r->data=p.allocate_bytes(r->bytes.size());p.memory.copy_in(r->data,r->bytes);}return r->data;}}
    stop(std::string("unimplemented ")+std::to_string(unsigned(r->kind))+"::"+methods(r->kind)[slot].name);
}
U32 Graphics::device(unsigned slot,Args a){switch(slot){
    case 3:return 0;case 4:return 256u*1024*1024;case 5:return 0;case 6:return out_res(a[1],root);case 7:return output(a[1],caps());
    case 8:{SDL_DisplayMode m{};if(SDL_GetCurrentDisplayMode(0,&m))return U32(D3DERR_INVALIDCALL);return output(a[2],D3DDISPLAYMODE{U32(m.w),U32(m.h),U32(m.refresh_rate),D3DFMT_X8R8G8B8});}
    case 9:return output(a[1],std::array<U32,4>{0,D3DDEVTYPE_HAL,focus_window,creation_flags});
    case 11:host->source_cursor(std::bit_cast<std::int32_t>(a[1]),std::bit_cast<std::int32_t>(a[2]));return 0;
    case 12:return host->cursor_visible(a[1]!=0)>=0;case 15:return 1;
    case 17:return present(a);case 18:if(a[1] || a[2] || a[3]!=D3DBACKBUFFER_TYPE_MONO)return U32(D3DERR_INVALIDCALL);return out_res(a[4],backbuffer);
    case 19:return output(a[2],D3DRASTER_STATUS{0,0});case 20:return 0;
    case 23:p.memory.check(a[7],4,Memory::Write);if(a[8])return U32(D3DERR_INVALIDCALL);return out_res(a[7],texture(a[1],a[2],a[3],a[4],D3DFORMAT(a[5]),a[6]));
    case 26:case 27:{p.memory.check(a[5],4,Memory::Write);if(!a[1] || a[1]>MaxBytes || a[6])return U32(D3DERR_INVALIDCALL);auto r=make(slot==26?Kind::vertexbuffer:Kind::indexbuffer);r->bytes.resize(a[1]);r->usage=a[2];r->format=slot==26?D3DFMT_VERTEXDATA:a[3];r->fvf=slot==26?a[3]:0;r->pool=a[4];return out_res(a[5],r);}
    case 28:case 29:{p.memory.check(a[7],4,Memory::Write);if(a[4] || a[5] || a[8])return U32(D3DERR_INVALIDCALL);return out_res(a[7],surface(new_image(a[1],a[2],D3DFORMAT(a[3]),slot==28?D3DUSAGE_RENDERTARGET:D3DUSAGE_DEPTHSTENCIL,D3DPOOL_DEFAULT)));}
    case 30:{auto src=image(get(a[1],Kind::surface)),dst=image(get(a[3],Kind::surface));auto sr=rectangle(a[2],src);POINT pt=a[4]?read<POINT>(p,a[4]):POINT{};if(!valid(sr,src->width,src->height))return U32(D3DERR_INVALIDCALL);download(src);return copy(dst,{pt.x,pt.y,pt.x+sr.right-sr.left,pt.y+sr.bottom-sr.top},src->bytes.data(),src->width*bpp(src->format),src->format,sr,1,0);}
    case 31:{auto src=get(a[1],Kind::texture),dst=get(a[2],Kind::texture);if(src->levels.size()!=dst->levels.size())return U32(D3DERR_INVALIDCALL);for(unsigned n=0;n<src->levels.size();++n){auto s=image(src,n),d=image(dst,n);download(s);const auto hr=copy(d,rectangle(0,d),s->bytes.data(),s->width*bpp(s->format),s->format,rectangle(0,s),1,0);if(hr)return hr;}return 0;}
    case 32:{auto src=image(get(a[1],Kind::surface)),dst=image(get(a[2],Kind::surface));if(src->width!=dst->width || src->height!=dst->height || src->format!=dst->format)return U32(D3DERR_INVALIDCALL);download(src);dst->bytes=src->bytes;dst->cpu_dirty=true;dst->gpu_dirty=false;return 0;}
    case 34:{auto src=image(get(a[1],Kind::surface)),dst=image(get(a[3],Kind::surface));auto sr=rectangle(a[2],src),dr=rectangle(a[4],dst);if(src==dst || !valid(sr,src->width,src->height) || !valid(dr,dst->width,dst->height) || depth_format(src->format) || depth_format(dst->format))return U32(D3DERR_INVALIDCALL);upload(src);upload(dst);fbo(src);fbo(dst);gl.BindFramebuffer(GL_READ_FRAMEBUFFER,src->fbo);gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,dst->fbo);gl.Disable(GL_SCISSOR_TEST);gl.BlitFramebuffer(sr.left,sr.top,sr.right,sr.bottom,dr.left,dr.top,dr.right,dr.bottom,GL_COLOR_BUFFER_BIT,a[5]==D3DTEXF_LINEAR?GL_LINEAR:GL_NEAREST);if(gl.GetError())stop("framebuffer blit failed");if(!alpha_format(dst->format)){gl.BindFramebuffer(GL_FRAMEBUFFER,dst->fbo);gl.ColorMask(0,0,0,1);gl.ClearColor(0,0,0,1);gl.Clear(GL_COLOR_BUFFER_BIT);}dst->gpu_dirty=true;dst->cpu_dirty=false;last_blit_source=sr;last_blit_destination=dr;last_source_width=src->width;last_source_height=src->height;last_destination_width=dst->width;last_destination_height=dst->height;return 0;}
    case 35:{auto dst=image(get(a[1],Kind::surface));auto r=rectangle(a[2],dst);if(!valid(r,dst->width,dst->height))return U32(D3DERR_INVALIDCALL);download(dst);const std::array<std::uint8_t,4> c{std::uint8_t(a[3]>>16),std::uint8_t(a[3]>>8),std::uint8_t(a[3]),std::uint8_t(a[3]>>24)};for(LONG y=r.top;y<r.bottom;++y)for(LONG x=r.left;x<r.right;++x)encode(dst->bytes.data()+(std::size_t(y)*dst->width+x)*bpp(dst->format),dst->format,c);dst->cpu_dirty=true;dst->gpu_dirty=false;return 0;}
    case 36:p.memory.check(a[5],4,Memory::Write);if(a[6])return U32(D3DERR_INVALIDCALL);return out_res(a[5],surface(new_image(a[1],a[2],D3DFORMAT(a[3]),0,a[4])));
    case 37:{if(a[1] || !a[2])return U32(D3DERR_INVALIDCALL);auto target=get(a[2],Kind::surface);const auto i=image(target);const D3DVIEWPORT9 viewport{0,0,i->width,i->height,0,1};if(state.target!=target || std::memcmp(&state.viewport,&viewport,sizeof(viewport)))++config_version;state.target=std::move(target);state.viewport=viewport;return 0;}
    case 38:return a[1]?U32(D3DERR_INVALIDCALL):out_res(a[2],state.target);
    case 39:{auto depth=get(a[1],Kind::surface,true);if(state.depth!=depth)++config_version;state.depth=std::move(depth);return 0;}case 40:return out_res(a[1],state.depth);
    case 41:if(state.scene)return U32(D3DERR_INVALIDCALL);state.scene=true;return 0;case 42:if(!state.scene)return U32(D3DERR_INVALIDCALL);state.scene=false;return 0;
    case 43:{auto i=image(state.target);upload(i);fbo(i);gl.ColorMask(1,1,1,1);gl.DepthMask(1);gl.StencilMask(~0u);gl.Disable(GL_SCISSOR_TEST);const auto c=a[4];gl.ClearColor(float((c>>16)&255)/255,float((c>>8)&255)/255,float(c&255)/255,alpha_format(i->format)?float(c>>24)/255:1.f);gl.ClearDepth(real(a[5]));gl.ClearStencil(GLint(a[6]));GLbitfield bits=0;if(a[3]&D3DCLEAR_TARGET)bits|=GL_COLOR_BUFFER_BIT;if(a[3]&D3DCLEAR_ZBUFFER)bits|=GL_DEPTH_BUFFER_BIT;if(a[3]&D3DCLEAR_STENCIL)bits|=GL_STENCIL_BUFFER_BIT;
        if(a[1]){auto rects=array<D3DRECT>(a[2],a[1]);gl.Enable(GL_SCISSOR_TEST);for(const auto& r:rects){gl.Scissor(r.x1,r.y1,r.x2-r.x1,r.y2-r.y1);gl.Clear(bits);}gl.Disable(GL_SCISSOR_TEST);}else gl.Clear(bits);i->gpu_dirty=true;return 0;}
    case 44:case 46:{auto m=read<D3DMATRIX>(p,a[2]);if(slot==46){const auto old=state.transforms[a[1]];D3DMATRIX out{};for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)for(unsigned n=0;n<4;++n)out.m[y][x]+=old.m[y][n]*m.m[n][x];m=out;}if(auto old=state.transforms.find(a[1]);old==state.transforms.end() || std::memcmp(&old->second,&m,sizeof(m)))++config_version;state.transforms[a[1]]=m;return 0;}
    case 45:return output(a[2],state.transforms[a[1]]);
    case 47:{auto v=read<D3DVIEWPORT9>(p,a[1]);auto i=image(state.target);if(!v.Width || !v.Height || std::uint64_t(v.X)+v.Width>i->width || std::uint64_t(v.Y)+v.Height>i->height)return U32(D3DERR_INVALIDCALL);if(std::memcmp(&state.viewport,&v,sizeof(v)))++config_version;state.viewport=v;return 0;}
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
    case 87:{auto declaration=get(a[1],Kind::declaration,true);if(state.declaration!=declaration || state.fvf)++config_version;state.declaration=std::move(declaration);state.fvf=0;return 0;}case 88:return out_res(a[1],state.declaration);
    case 89:if(state.fvf!=a[1] || state.declaration)++config_version;state.fvf=a[1];state.declaration.reset();return 0;case 90:return output(a[1],state.fvf);
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
        if(writing){constant_bytes.resize(count);p.memory.copy_out(a[2],constant_bytes);
            if(wa2::graphics::update_shader_constants(std::span(static_cast<std::uint8_t*>(target),count),constant_bytes)){++config_version;++constant_updates;}else ++constant_reuses;
        }else p.memory.copy_in(a[2],std::span(static_cast<const std::uint8_t*>(target),count));return 0;
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
void Graphics::bind_sampler(unsigned n,const Res& texture,const std::array<U32,14>* parameters){
    const auto& s=parameters?*parameters:state.sampler[n];for(const auto& level:texture->levels)upload(level);
    const auto object=image(texture)->texture;
    gl.ActiveTexture(GL_TEXTURE0+n);gl.BindTexture(GL_TEXTURE_2D,object->id);
    const auto address=[&](U32 mode)->GLint{switch(mode){case D3DTADDRESS_WRAP:return GL_REPEAT;case D3DTADDRESS_MIRROR:return GL_MIRRORED_REPEAT;case D3DTADDRESS_CLAMP:return GL_CLAMP_TO_EDGE;case D3DTADDRESS_BORDER:if(gl.embedded)stop("GLES border addressing requires the border-sampler shader contract");return GL_CLAMP_TO_BORDER;default:stop("sampler address mode");}};
    GLint min=s[D3DSAMP_MINFILTER]==D3DTEXF_POINT?GL_NEAREST:GL_LINEAR;
    if(s[D3DSAMP_MIPFILTER] && texture->levels.size()>1)min=s[D3DSAMP_MINFILTER]==D3DTEXF_POINT?(s[D3DSAMP_MIPFILTER]==D3DTEXF_POINT?GL_NEAREST_MIPMAP_NEAREST:GL_NEAREST_MIPMAP_LINEAR):(s[D3DSAMP_MIPFILTER]==D3DTEXF_POINT?GL_LINEAR_MIPMAP_NEAREST:GL_LINEAR_MIPMAP_LINEAR);
    const Texture::Parameters requested{address(s[D3DSAMP_ADDRESSU]),address(s[D3DSAMP_ADDRESSV]),min,
        s[D3DSAMP_MAGFILTER]==D3DTEXF_POINT?GL_NEAREST:GL_LINEAR,s[D3DSAMP_BORDERCOLOR]};
    const auto previous=sampler_cache_enabled?object->parameters:std::nullopt;
    auto parameter=[&](GLenum name,GLint value,GLint old){if(!previous || value!=old){gl.TexParameteri(GL_TEXTURE_2D,name,value);++sampler_parameter_calls;}else ++sampler_parameter_skips;};
    parameter(GL_TEXTURE_WRAP_S,requested.s,previous?previous->s:0);
    parameter(GL_TEXTURE_WRAP_T,requested.t,previous?previous->t:0);
    parameter(GL_TEXTURE_MIN_FILTER,requested.min,previous?previous->min:0);
    parameter(GL_TEXTURE_MAG_FILTER,requested.mag,previous?previous->mag:0);
    if(!gl.embedded && (!previous || requested.border!=previous->border)){
        const auto color=requested.border;const std::array<float,4> border{float((color>>16)&255)/255,float((color>>8)&255)/255,float(color&255)/255,float(color>>24)/255};
        gl.TexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,border.data());++sampler_parameter_calls;
    }else ++sampler_parameter_skips;
    // Parameters belong to the shared GL object, not the D3D binding slot.
    // Recording the actual last applied state retains ordered alias behavior.
    object->parameters=requested;
}
void Graphics::apply(){
    FrameTrace::Timer timing(frame_trace.enabled?&frame_trace.apply:nullptr);
    if(has_pending() && (!applied || config_version!=applied_config_version || word_differences || texture_differences))flush();
    auto target=image(state.target);upload(target);
    const auto mode=wa2::graphics::submission_apply(bool(applied),config_version==applied_config_version,
        word_differences,texture_differences,!state.pixel_shader,wa2::graphics::texture_mask(state.textures),
        applied?wa2::graphics::texture_mask(applied->textures):0);
    if(mode==wa2::graphics::SubmissionApply::textures){
        wa2::graphics::refresh_texture_bindings(state.textures,applied->textures,
            [&](unsigned n,const Res& texture){gl.ActiveTexture(GL_TEXTURE0+n);bind_sampler(n,texture);},
            [&](unsigned n){gl.ActiveTexture(GL_TEXTURE0+n);gl.BindTexture(GL_TEXTURE_2D,0);});
        gl.ActiveTexture(GL_TEXTURE0);applied->textures=state.textures;baseline_textures=state.textures;
        texture_differences=0;applied_texture_version=texture_version;++texture_only_applies;++state_cache_hits;return;
    }
    if(mode==wa2::graphics::SubmissionApply::reuse){
        for(unsigned n=0;n<16;++n)if(state.textures[n])for(const auto& level:state.textures[n]->levels)upload(level);
        if(applied){++state_cache_hits;return;}
    }
    ++state_applies;fbo(target);GLuint depth_id{};
    if(state.depth){auto d=image(state.depth);if(d->width!=target->width || d->height!=target->height)stop("depth/target size mismatch");depth_id=d->texture->id;}
    if(target->attached_depth!=depth_id){gl.FramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_TEXTURE_2D,depth_id,0);target->attached_depth=depth_id;
        ++framebuffer_checks;if(gl.CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)stop("incomplete depth framebuffer");}
    const auto& v=state.viewport;gl.Viewport(v.X,v.Y,v.Width,v.Height);gl.DepthRange(v.MinZ,v.MaxZ);
    auto enable=[this](GLenum cap,bool on){if(on)gl.Enable(cap);else gl.Disable(cap);};const auto& r=state.render;
    enable(GL_DEPTH_TEST,r[D3DRS_ZENABLE] && bool(state.depth));gl.DepthMask(r[D3DRS_ZWRITEENABLE]!=0);gl.DepthFunc(GLenum(GL_NEVER+r[D3DRS_ZFUNC]-1));
    enable(GL_CULL_FACE,r[D3DRS_CULLMODE]!=D3DCULL_NONE);gl.CullFace(GL_BACK);gl.FrontFace(r[D3DRS_CULLMODE]==D3DCULL_CW?GL_CW:GL_CCW);
    if(r[D3DRS_FILLMODE]!=D3DFILL_SOLID || r[D3DRS_SHADEMODE]!=D3DSHADE_GOURAUD)stop("unsupported non-solid or flat rasterization");
    if(r[D3DRS_LIGHTING] || r[D3DRS_FOGENABLE] || r[D3DRS_STENCILENABLE])stop("unsupported lighting/fog/stencil pipeline");
    enable(GL_SCISSOR_TEST,r[D3DRS_SCISSORTESTENABLE]!=0);gl.Scissor(state.scissor.left,state.scissor.top,state.scissor.right-state.scissor.left,state.scissor.bottom-state.scissor.top);
    const auto mask=r[D3DRS_COLORWRITEENABLE];gl.ColorMask(mask&1,mask&2,mask&4,(mask&8) && alpha_format(target->format));enable(GL_BLEND,r[D3DRS_ALPHABLENDENABLE]!=0);
    auto blend=[&](U32 b)->GLenum{switch(b){case D3DBLEND_ZERO:return GL_ZERO;case D3DBLEND_ONE:return GL_ONE;case D3DBLEND_SRCCOLOR:return GL_SRC_COLOR;case D3DBLEND_INVSRCCOLOR:return GL_ONE_MINUS_SRC_COLOR;case D3DBLEND_SRCALPHA:case D3DBLEND_BOTHSRCALPHA:return GL_SRC_ALPHA;case D3DBLEND_INVSRCALPHA:case D3DBLEND_BOTHINVSRCALPHA:return GL_ONE_MINUS_SRC_ALPHA;case D3DBLEND_DESTALPHA:return GL_DST_ALPHA;case D3DBLEND_INVDESTALPHA:return GL_ONE_MINUS_DST_ALPHA;case D3DBLEND_DESTCOLOR:return GL_DST_COLOR;case D3DBLEND_INVDESTCOLOR:return GL_ONE_MINUS_DST_COLOR;case D3DBLEND_SRCALPHASAT:return GL_SRC_ALPHA_SATURATE;case D3DBLEND_BLENDFACTOR:return GL_CONSTANT_COLOR;case D3DBLEND_INVBLENDFACTOR:return GL_ONE_MINUS_CONSTANT_COLOR;default:stop("blend factor "+std::to_string(b));}};
    auto equation=[&](U32 op)->GLenum{switch(op){case D3DBLENDOP_ADD:return GL_FUNC_ADD;case D3DBLENDOP_SUBTRACT:return GL_FUNC_SUBTRACT;case D3DBLENDOP_REVSUBTRACT:return GL_FUNC_REVERSE_SUBTRACT;case D3DBLENDOP_MIN:return GL_MIN;case D3DBLENDOP_MAX:return GL_MAX;default:stop("blend equation");}};
    const auto color=r[D3DRS_BLENDFACTOR];gl.BlendColor(float((color>>16)&255)/255,float((color>>8)&255)/255,float(color&255)/255,float(color>>24)/255);
    const auto src=blend(r[D3DRS_SRCBLEND]),dst=blend(r[D3DRS_DESTBLEND]);gl.BlendFuncSeparate(src,dst,r[D3DRS_SEPARATEALPHABLENDENABLE]?blend(r[D3DRS_SRCBLENDALPHA]):src,r[D3DRS_SEPARATEALPHABLENDENABLE]?blend(r[D3DRS_DESTBLENDALPHA]):dst);gl.BlendEquationSeparate(equation(r[D3DRS_BLENDOP]),equation(r[D3DRS_SEPARATEALPHABLENDENABLE]?r[D3DRS_BLENDOPALPHA]:r[D3DRS_BLENDOP]));
    prepare_program();
    for(unsigned n=0;n<16;++n){gl.ActiveTexture(GL_TEXTURE0+n);if(state.textures[n])bind_sampler(n,state.textures[n]);else gl.BindTexture(GL_TEXTURE_2D,0);}
    gl.ActiveTexture(GL_TEXTURE0);
    applied=state;applied_config_version=config_version;applied_texture_version=texture_version;reset_baseline();
}

void Graphics::flush() {
    if(!has_pending())return;
    FrameTrace::Timer timing(frame_trace.enabled?&frame_trace.flush:nullptr);
    // Pending vertices have already been transformed using the state that was
    // applied when they were queued. Later setters only change CPU state.
    gl.BindBuffer(GL_ARRAY_BUFFER,vertex_buffer);
    const bool multiple=!texture_batch.empty(),compact=multiple || !pending_screen.empty();
    const auto count=multiple?texture_batch.indices().size():compact?pending_screen_indices.size():pending_vertices.size();
    const auto bytes=multiple?texture_batch.vertices().size_bytes():compact?pending_screen.size()*sizeof(wa2::graphics::ScreenVertex):pending_vertices.size()*sizeof(FixedVertex);
    const void* data=multiple?static_cast<const void*>(texture_batch.vertices().data()):compact?static_cast<const void*>(pending_screen.data()):pending_vertices.data();
    gl.BufferData(GL_ARRAY_BUFFER,GLsizeiptr(bytes),data,GL_STREAM_DRAW);
    vertex_upload_bytes+=bytes;
    if(compact){
        using V=wa2::graphics::ScreenVertex;const unsigned stride=multiple?sizeof(wa2::graphics::TextureBatchVertex):sizeof(V);
        MOJOSHADER_glSetVertexAttribute(MOJOSHADER_USAGE_POSITION,0,4,MOJOSHADER_ATTRIBUTE_FLOAT,0,stride,reinterpret_cast<const void*>(offsetof(V,position)));
        MOJOSHADER_glSetVertexAttribute(MOJOSHADER_USAGE_COLOR,0,4,MOJOSHADER_ATTRIBUTE_UBYTE,1,stride,reinterpret_cast<const void*>(offsetof(V,colour)));
        // Admission guarantees that the fixed pixel shader consumes only
        // these two texture coordinates; linked-out attributes need no array.
        for(unsigned n=0;n<2;++n)MOJOSHADER_glSetVertexAttribute(MOJOSHADER_USAGE_TEXCOORD,n,2,MOJOSHADER_ATTRIBUTE_FLOAT,0,stride,
            reinterpret_cast<const void*>(offsetof(V,texture)+n*sizeof(std::array<float,2>)));
        if(multiple)MOJOSHADER_glSetVertexAttribute(MOJOSHADER_USAGE_TEXCOORD,7,1,MOJOSHADER_ATTRIBUTE_FLOAT,0,stride,
            reinterpret_cast<const void*>(offsetof(wa2::graphics::TextureBatchVertex,binding)));
    }else{
        const unsigned stride=sizeof(FixedVertex);
        MOJOSHADER_glSetVertexAttribute(MOJOSHADER_USAGE_POSITION,0,4,MOJOSHADER_ATTRIBUTE_FLOAT,0,stride,reinterpret_cast<const void*>(offsetof(FixedVertex,pos)));
        MOJOSHADER_glSetVertexAttribute(MOJOSHADER_USAGE_COLOR,0,4,MOJOSHADER_ATTRIBUTE_FLOAT,0,stride,reinterpret_cast<const void*>(offsetof(FixedVertex,color)));
        for(unsigned n=0;n<8;++n)MOJOSHADER_glSetVertexAttribute(MOJOSHADER_USAGE_TEXCOORD,n,4,MOJOSHADER_ATTRIBUTE_FLOAT,0,stride,
            reinterpret_cast<const void*>(offsetof(FixedVertex,uv)+n*sizeof(std::array<float,4>)));
    }
    if(multiple){
        if(!applied)stop("missing texture batch state");
        // Setters can already describe the next draw. Bind the saved sampler
        // of this pending packet, with its own retained resource references.
        for(unsigned n=0;n<wa2::graphics::TextureBatchSlots;++n){
            if(n<texture_batch.binding_count())bind_sampler(n,texture_batch.bindings()[n],&applied->sampler[0]);
            else{gl.ActiveTexture(GL_TEXTURE0+n);gl.BindTexture(GL_TEXTURE_2D,0);}
        }
        gl.ActiveTexture(GL_TEXTURE0);
    }
    MOJOSHADER_glProgramReady();
    const auto& viewport=applied?applied->viewport:state.viewport;
    MOJOSHADER_glProgramViewportInfo(viewport.Width,viewport.Height,image(backbuffer)->width,image(backbuffer)->height,1);
    if(compact){
        gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER,index_buffer);
        const auto indices=multiple?texture_batch.indices():std::span<const std::uint16_t>(pending_screen_indices);
        const auto size=indices.size_bytes();
        gl.BufferData(GL_ELEMENT_ARRAY_BUFFER,GLsizeiptr(size),indices.data(),GL_STREAM_DRAW);
        index_upload_bytes+=size;gl.DrawElements(GL_TRIANGLES,GLsizei(count),GL_UNSIGNED_SHORT,nullptr);
    }else gl.DrawArrays(GL_TRIANGLES,0,GLsizei(count));
    ++submitted_draws;submitted_vertices+=count;
    if(pending_target)pending_target->levels[0]->gpu_dirty=true;
    pending_vertices.clear();pending_screen.clear();pending_screen_indices.clear();pending_target.reset();
    if(multiple){++texture_batch_submissions;texture_batch.clear();applied.reset();}
}
bool Graphics::batch_textures(std::span<const wa2::graphics::ScreenVertex> vertices,U32 type,U32 count,std::span<const U32> indices){
    if(!texture_batch_available || state.vertex_shader || state.pixel_shader || !state.textures[0])return false;
    if(image(state.textures[0])->texture==image(state.target)->texture)return false;
    wa2::graphics::FixedPipeline pipeline{};
    for(unsigned n=0;n<8;++n){const auto& t=state.stage[n];pipeline.stages[n]={t[D3DTSS_COLOROP],t[D3DTSS_COLORARG1],t[D3DTSS_COLORARG2],
        t[D3DTSS_ALPHAOP],t[D3DTSS_ALPHAARG1],t[D3DTSS_ALPHAARG2],t[D3DTSS_TEXTURETRANSFORMFLAGS],bool(state.textures[n])};}
    if(!wa2::graphics::fixed_pipeline_texture_batch(pipeline,wa2::graphics::texture_mask(state.textures)))return false;
    const auto elements=decltype(texture_batch)::index_count(type,count);if(!elements)return false;
    if(!texture_batch.empty() && (!applied || config_version!=applied_config_version || word_differences))flush();
    if(!texture_batch.fits(state.textures[0],unsigned(vertices.size()),elements))flush();
    if(!texture_batch.fits(state.textures[0],unsigned(vertices.size()),elements))return false;
    texture_mode(true);
    if(texture_batch.empty()){
        try{apply();}
        catch(const GuestFault& error){
            // This is an optional native submission optimization. Older or
            // buggy shader drivers retain the original shared draw path.
            texture_batch_failure=error.what();texture_batch_available=false;texture_batch_mode=false;applied.reset();return false;
        }
        pending_target=state.target;
    }
    if(!texture_batch.append(state.textures[0],vertices,type,count,indices))stop("invalid bounded texture batch packet");
    ++texture_batch_draws;return true;
}
bool Graphics::sprite_command(const wa2::aot::SpriteCommand& command){
    if(!context || !state.scene || state.vertex_shader || state.pixel_shader || command.tiles.size()>256)return false;
    for(unsigned n=1;n<8;++n)if(state.stage[n][D3DTSS_COLOROP]!=D3DTOP_DISABLE)return false;
    for(unsigned n=1;n<16;++n)if(state.textures[n])return false;
    const auto device=get(command.device,Kind::device);
    const bool offscreen=command.target_kind==wa2::aot::SpriteTarget::texture_level_zero;
    const auto target_resource=get(command.target,offscreen?Kind::texture:Kind::surface);
    if(device!=device_resource)return false;
    std::array<Res,256> textures;
    for(unsigned n=0;n<command.tiles.size();++n){textures[n]=get(command.tiles[n].texture,Kind::texture);
        if(image(textures[n])->texture==image(target_resource)->texture)return false;}
    enter(false);
    const auto target=offscreen?surface(image(target_resource),target_resource):target_resource;
    // Preserve the source GetSurfaceLevel output, interface lifetime and
    // Release even though the state change itself uses a typed native packet.
    U32 target_interface{};if(offscreen){target_interface=wrap(target);p.memory.store(0xb34008,target_interface,32);}
    const auto i=image(target);const D3DVIEWPORT9 viewport{0,0,i->width,i->height,0,1};
    if(state.target!=target || std::memcmp(&state.viewport,&viewport,sizeof(viewport)))++config_version;
    state.target=target;state.viewport=viewport;
    if(offscreen){const std::array<U32,1> args{target_interface};method(object(target_interface),2,args);}
    wa2::aot::sprite_state_words(command,[&](auto group,U32 key,U32 value){
        using Group=wa2::aot::SpriteState;
        if(group==Group::render)set_word(state.render[key],value,baseline_words.render[key]);
        else if(group==Group::stage)set_word(state.stage[0][key],value,baseline_words.stage[0][key]);
        else set_word(state.sampler[0][key],value,baseline_words.sampler[0][key]);
    });
    for(unsigned n=0;n<command.tiles.size();++n){const auto& tile=command.tiles[n];
        set_texture(0,textures[n],tile.texture);
        if(state.fvf!=0x1c4 || state.declaration)++config_version;state.fvf=0x1c4;state.declaration.reset();
        const auto result=draw(D3DPT_TRIANGLESTRIP,4,{reinterpret_cast<const std::uint8_t*>(tile.vertices.data()),sizeof(tile.vertices)},sizeof(wa2::aot::SpriteVertex),{});
        if(result)stop("invalid native sprite packet");state.streams[0]={};set_texture(0,{},0);
    }
    return true;
}
U32 Graphics::draw(U32 type,U32 count,std::span<const std::uint8_t> bytes,U32 stride,std::span<const U32> indices){
    FrameTrace::Timer timing(frame_trace.enabled?&frame_trace.draw:nullptr);
    if(!state.scene || !stride || stride>256 || bytes.size()%stride)return U32(D3DERR_INVALIDCALL);if(!count)return 0;const auto total=bytes.size()/stride;
    if((indices.empty() && count>total) || (!indices.empty() && indices.size()!=count))return U32(D3DERR_INVALIDCALL);
    // The fixed vertex stage consumes the same complete position/colour/UV
    // stream with a programmable pixel shader too. Pipeline/constant/resource
    // barriers still flush in order; only programmable vertex stages require
    // the generic unbatched input layout.
    const bool batchable=wa2::graphics::batch_fixed_vertex_stage(bool(state.vertex_shader),type);
    bool simple=batchable && !state.pixel_shader && !state.declaration && total<=4096 && count<=4096 && (indices.empty() || total<=count*2);
    for(unsigned n=0;n<8 && simple;++n){const auto& stage=state.stage[n];
        simple=stage[D3DTSS_TEXCOORDINDEX]<8 && !stage[D3DTSS_TEXTURETRANSFORMFLAGS] &&
            (n<2 || stage[D3DTSS_COLOROP]==D3DTOP_DISABLE);
    }
    const auto plan=simple?wa2::graphics::screen_vertex_plan(state.fvf,state.viewport,
        {state.stage[0][D3DTSS_TEXCOORDINDEX],state.stage[1][D3DTSS_TEXCOORDINDEX]}):std::nullopt;
    if(plan && stride>=plan->source_prefix+plan->source_coordinates*8){
        for(auto index:indices)if(index>=total)return U32(D3DERR_INVALIDCALL);
        screen_scratch.resize(total);
        if(wa2::graphics::pack_screen_vertices(*plan,bytes,stride,screen_scratch)){
            if(batch_textures(screen_scratch,type,count,indices)){
                ++screen_draws;screen_unique_vertices+=total;++batched_guest_draws;
                image(state.target)->gpu_dirty=true;++draws;return 0;
            }
            texture_mode(false);
            if(!pending_vertices.empty())flush();apply();
            const auto triangles=type==D3DPT_TRIANGLELIST?count/3:count>2?count-2:0;
            if(pending_screen.size()+total>4096 || pending_screen_indices.size()+triangles*3>12288)flush();
            if(pending_screen.empty())pending_target=state.target;
            const auto base=U32(pending_screen.size());
            pending_screen.insert(pending_screen.end(),screen_scratch.begin(),screen_scratch.end());
            const auto index=[&](U32 n){return std::uint16_t(base+(indices.empty()?n:indices[n]));};
            wa2::graphics::each_triangle(type,count,[&](U32 a,U32 b,U32 c){
                pending_screen_indices.push_back(index(a));pending_screen_indices.push_back(index(b));pending_screen_indices.push_back(index(c));
            });
            ++screen_draws;screen_unique_vertices+=total;++batched_guest_draws;
            image(state.target)->gpu_dirty=true;++draws;return 0;
        }
    }
    texture_mode(false);
    using Vertex=DecodedVertex;
    decoded_vertices.assign(total,Vertex{});auto& vertices=decoded_vertices;
    auto f32=[](const std::uint8_t* at){float v;std::memcpy(&v,at,4);return v;};
    auto read_color=[](const std::uint8_t* at){return std::array<float,4>{float(at[2])/255,float(at[1])/255,float(at[0])/255,float(at[3])/255};};
    for(std::size_t n=0;n<total;++n){auto& v=vertices[n];const auto* base=bytes.data()+n*stride;
        if(state.declaration){for(const auto& e:state.declaration->elements){if(e.Stream==0xff)break;if(e.Stream || e.Method!=D3DDECLMETHOD_DEFAULT)stop("vertex declaration stream/method");std::array<float,4>* dst=nullptr;if(e.Usage==D3DDECLUSAGE_POSITION || e.Usage==D3DDECLUSAGE_POSITIONT)dst=&v.pos;else if(e.Usage==D3DDECLUSAGE_NORMAL)dst=&v.normal;else if(e.Usage==D3DDECLUSAGE_COLOR)dst=e.UsageIndex?&v.specular:&v.color;else if(e.Usage==D3DDECLUSAGE_TEXCOORD && e.UsageIndex<8)dst=&v.uv[e.UsageIndex];else stop("vertex declaration usage");const auto size=e.Type<=D3DDECLTYPE_FLOAT4?(e.Type+1)*4:e.Type==D3DDECLTYPE_D3DCOLOR?4:0;if(!size || U32(e.Offset)+size>stride)stop("vertex declaration type/bounds");if(e.Type==D3DDECLTYPE_D3DCOLOR)*dst=read_color(base+e.Offset);else for(unsigned j=0;j<size/4;++j)(*dst)[j]=f32(base+e.Offset+j*4);}}
        else {unsigned offset=0;auto floats=[&](float* dst,unsigned size){if(offset+size*4>stride)stop("FVF stride");for(unsigned j=0;j<size;++j)dst[j]=f32(base+offset+j*4);offset+=size*4;};const auto pos=state.fvf&D3DFVF_POSITION_MASK;if(pos!=D3DFVF_XYZ && pos!=D3DFVF_XYZRHW)stop("FVF position type");floats(v.pos.data(),pos==D3DFVF_XYZRHW?4:3);if(state.fvf&D3DFVF_NORMAL)floats(v.normal.data(),3);if(state.fvf&D3DFVF_PSIZE){float unused;floats(&unused,1);}for(auto flag:{D3DFVF_DIFFUSE,D3DFVF_SPECULAR})if(state.fvf&flag){if(offset+4>stride)stop("FVF color stride");(flag==D3DFVF_DIFFUSE?v.color:v.specular)=read_color(base+offset);offset+=4;}const auto tex=(state.fvf&D3DFVF_TEXCOUNT_MASK)>>D3DFVF_TEXCOUNT_SHIFT;if(tex>8)stop("FVF texture count");for(unsigned j=0;j<tex;++j){const auto code=(state.fvf>>(16+j*2))&3;floats(v.uv[j].data(),code==0?2:code==1?3:code==2?4:1);}}
    }
    for(auto index:indices)if(index>=total)return U32(D3DERR_INVALIDCALL);
    if(!pending_screen.empty())flush();
    if(!batchable)flush();
    apply();const GLenum primitive=type==D3DPT_POINTLIST?GL_POINTS:type==D3DPT_LINELIST?GL_LINES:type==D3DPT_LINESTRIP?GL_LINE_STRIP:type==D3DPT_TRIANGLELIST?GL_TRIANGLES:type==D3DPT_TRIANGLESTRIP?GL_TRIANGLE_STRIP:GL_TRIANGLE_FAN;
    if(batchable){
        auto vertex=[&](U32 index){const auto v=fixed_vertex(vertices[indices.empty()?index:indices[index]]);return FixedVertex{v.pos,v.color,v.uv};};
        auto triangle=[&](U32 a,U32 b,U32 c){
            if(pending_vertices.size()+3>4096)flush();
            if(pending_vertices.empty())pending_target=state.target;
            pending_vertices.push_back(vertex(a));pending_vertices.push_back(vertex(b));pending_vertices.push_back(vertex(c));
        };
        wa2::graphics::each_triangle(type,count,triangle);
        ++batched_guest_draws;image(state.target)->gpu_dirty=true;++draws;return 0;
    }
    if(!state.vertex_shader)for(auto& vertex:vertices)vertex=fixed_vertex(vertex);
    submit(primitive,indices.empty()?std::span(vertices).first(count):std::span(vertices),indices);
    image(state.target)->gpu_dirty=true;++draws;return 0;
}

void Graphics::movie_frame(const wa2::movie::VideoFrame& frame,const wa2::graphics::MovieView& view){
    enter();if(!context || !backbuffer)stop("movie presentation requires the game render device");
    const auto owner=presentation[7]?presentation[7]:focus_window;
    if(view.owner && view.owner!=owner)stop("movie presentation to a different source window");
    if(!view.visible)return;
    int w=0,h=0;SDL_GL_GetDrawableSize(window,&w,&h);if(w<1 || h<1)return;
    const auto viewport=host->client_viewport(owner,w,h);
    const auto layout=wa2::graphics::layout_movie(frame,view,viewport);
    if(!layout)stop("invalid movie dimensions, crop, destination or source client");
    if(!movie_shader)movie_shader=assemble_shader(wa2::graphics::movie_pixel_assembly());
    if(!movie_planes[0])gl.GenTextures(3,movie_planes.data());
    for(unsigned n=0;n<3;++n){const auto& plane=frame.planes[n];if(!plane.pixels || plane.width<1 || plane.height<1 || plane.pitch<plane.width || plane.width>max_texture || plane.height>max_texture)stop("invalid decoded movie plane");
        gl.ActiveTexture(GL_TEXTURE0+n);gl.BindTexture(GL_TEXTURE_2D,movie_planes[n]);gl.PixelStorei(GL_UNPACK_ALIGNMENT,1);gl.PixelStorei(GL_UNPACK_ROW_LENGTH,plane.pitch);
        if(movie_dimensions[n]!=std::pair{plane.width,plane.height}){gl.TexImage2D(GL_TEXTURE_2D,0,GL_R8,plane.width,plane.height,0,GL_RED,GL_UNSIGNED_BYTE,plane.pixels);movie_dimensions[n]={plane.width,plane.height};}
        else gl.TexSubImage2D(GL_TEXTURE_2D,0,0,0,plane.width,plane.height,GL_RED,GL_UNSIGNED_BYTE,plane.pixels);
        gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);gl.TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH,0);gl.ActiveTexture(GL_TEXTURE0);
    const auto key=std::pair{default_vertex,movie_shader};auto program=programs.find(key);if(program==programs.end()){
        const auto shader=MOJOSHADER_glLinkProgram(default_vertex,movie_shader);if(!shader)stop(std::string("movie shader link: ")+MOJOSHADER_glGetError());
        const auto native=wa2_mojo_program_handle(shader);program=programs.emplace(key,Program{shader,gl.GetUniformLocation(native,"wa2_alpha_mode"),gl.GetUniformLocation(native,"wa2_alpha_reference")}).first;
    }
    MOJOSHADER_glBindProgram(program->second.shader);if(program->second.alpha_mode>=0)gl.Uniform1i(program->second.alpha_mode,0);
    const bool bt709=frame.matrix==wa2::movie::ColourMatrix::bt709;
    const auto constants=wa2::graphics::movie_colour_constants(frame.full_range,bt709);
    for(unsigned n=0;n<constants.size();++n)MOJOSHADER_glSetPixelShaderUniformF(n,constants[n].data(),1);
    const auto& destination=layout->destination;const auto& texture=layout->texture;
    const double x0=destination[0],y0=destination[1],x1=x0+destination[2],y1=y0+destination[3];
    const float u0=texture[0],v0=texture[1],u1=texture[2],v1=texture[3];
    const auto vertex=[&](double x,double y,float u,float v){DecodedVertex out;out.pos={float(x*2/w-1),float(1-y*2/h),0,1};out.uv[0]={u,v,0,1};return out;};
    const std::array<DecodedVertex,6> vertices{vertex(x0,y0,u0,v0),vertex(x1,y0,u1,v0),vertex(x1,y1,u1,v1),vertex(x0,y0,u0,v0),vertex(x1,y1,u1,v1),vertex(x0,y1,u0,v1)};
    gl.BindFramebuffer(GL_FRAMEBUFFER,0);gl.Viewport(0,0,w,h);gl.Disable(GL_SCISSOR_TEST);gl.Disable(GL_DEPTH_TEST);gl.Disable(GL_BLEND);gl.Disable(GL_CULL_FACE);gl.ColorMask(1,1,1,1);gl.ClearColor(0,0,0,1);gl.Clear(GL_COLOR_BUFFER_BIT);
    submit(GL_TRIANGLES,vertices,{});applied.reset();const auto error=gl.GetError();if(error)stop("GL movie frame error "+std::to_string(error));
    ++movie_frames;last_drawable_size={w,h};last_movie_client=viewport.size;last_movie_size={frame.width,frame.height};
    last_movie_source=view.source;last_movie_requested=view.destination;last_movie_destination=destination;
    SDL_GL_SwapWindow(window);host->frame_submitted();
}
void Graphics::restore_game(){if(context && backbuffer && !stopped)present(std::array<U32,5>{});}
U32 Graphics::present(Args a){
    const auto present_begin=FrameTrace::now();
    frame_trace.source_fp_control=p.cpu.fp.get16(0);
    if(a[4])stop("Present dirty regions");if(a[3] && a[3]!=(presentation[7]?presentation[7]:focus_window))stop("Present to another window");
    flush();applied.reset();auto i=image(backbuffer);upload(i);fbo(i);
    int w=0,h=0;SDL_GL_GetDrawableSize(window,&w,&h);if(w<=0 || h<=0)return 0;
    auto sr=rectangle(a[1],i);if(!valid(sr,i->width,i->height))return U32(D3DERR_INVALIDCALL);
    const auto viewport=host->client_viewport(presentation[7]?presentation[7]:focus_window,w,h);
    std::optional<wa2::graphics::FrameRect> destination;if(a[2]){const auto r=read<RECT>(p,a[2]);destination=wa2::graphics::FrameRect{r.left,r.top,r.right,r.bottom};}
    const auto layout=wa2::graphics::layout_frame(i->width,i->height,{sr.left,sr.top,sr.right,sr.bottom},destination,viewport);
    if(!layout)return U32(D3DERR_INVALIDCALL);const auto& d=layout->destination;const auto left=d[0],top=d[1],right=d[2],bottom=d[3];
    last_drawable_size={w,h};last_present_source={sr.left,sr.top,sr.right,sr.bottom};last_present_destination={left,top,right,bottom};
    gl.Disable(GL_SCISSOR_TEST);gl.ColorMask(1,1,1,1);gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER,0);gl.ClearColor(0,0,0,1);gl.Clear(GL_COLOR_BUFFER_BIT);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER,i->fbo);
    // Source D3D top rows reside at low texture Y. Flip only presentation.
    gl.BlitFramebuffer(sr.left,sr.top,sr.right,sr.bottom,left,h-top,right,h-bottom,GL_COLOR_BUFFER_BIT,GL_LINEAR);
    const auto before_error=FrameTrace::now();++frame_error_checks;const auto error=gl.GetError();if(error)stop("GL frame error "+std::to_string(error));
    const auto after_error=FrameTrace::now();SDL_GL_SwapWindow(window);const auto after_swap=FrameTrace::now();++presents;
    frame_trace.presented(presents,present_begin,before_error,after_error,after_swap,copy_total.total_ns,draws,uploads,
        {p.native_function_entries,p.api_calls,p.direct_leaf_api_calls,p.host_work.sleep_wall_ns,p.host_work.wait_wall_ns,p.host_work.sleep_requested_ms,p.host_work.sleep_zero_calls,p.host_work.function_wall_ns,p.host_work.api_wall_ns,p.host_work.poll_wall_ns});host->frame_submitted();
    if(p.state().stop_after_presents && presents>=p.state().stop_after_presents){p.state().diagnostic_stop=true;p.exit(0);}return 0;
}
void Graphics::install(){
    sprite_service.emplace(p.memory,[](const wa2::aot::SpriteCommand& command,void* owner){return static_cast<Graphics*>(owner)->sprite_command(command);},this);
    // Bind implementations before load so their leaf contracts belong to
    // these exact handlers. Guest vtables remain lazy source-memory objects.
    for(unsigned value=0;value<=unsigned(Kind::resource);++value){const auto kind=Kind(value);const auto table=methods(kind);
        for(unsigned slot=0;slot<table.size();++slot){const auto name=method_name(kind,table[slot].name);
            auto* count=&calls[std::to_string(value)+"."+table[slot].name];
            api(p,"wa2.d3d9",name,table[slot].arguments,[this,kind,slot,count](Process&,Args a){
                const bool metadata=(kind==Kind::texture && wa2::graphics::texture_metadata_method(slot)) ||
                    (kind==Kind::surface && wa2::graphics::surface_metadata_method(slot));
                enter(false,!metadata && (kind!=Kind::device || !wa2::graphics::context_free_device_method(slot)));
                auto& o=object(a[0]);if(o.resource->kind!=kind)stop("wrong vtable");++*count;return method(o,slot,a);});
            if(kind==Kind::device && wa2::graphics::direct_device_method(slot))p.certify_leaf_api("wa2.d3d9",name);
        }
    }
    p.state().reports["frame_timing"]=[this]{return frame_trace.report();};
    p.state().reports["resource_graphics"]=[this]{return "{\"surface_load_calls\":"+std::to_string(copy_total.calls)+
        ",\"pixels_processed\":"+std::to_string(copy_total.pixels)+",\"surface_load_ns\":"+std::to_string(copy_total.total_ns)+
        ",\"readback_ns\":"+std::to_string(copy_total.readback_ns)+",\"direct_source_copies\":"+std::to_string(direct_source_copies)+
        ",\"source_staging_bytes_avoided\":"+std::to_string(source_staging_bytes_avoided)+",\"screen_capture\":false}";};
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
        if(p.options.profile_execution)source_bytes_before_crop+=std::uint64_t(original.bottom)*a[5];
        // A bad destination pointer used to be reported only after staging the
        // source. Do not change that error order when declining the shortcut.
        std::optional<RECT> direct_rectangle;
        try{direct_rectangle=rectangle(a[2],dst);}catch(const GuestFault&){}
        const auto order=[](D3DFORMAT format){return format==D3DFMT_A8R8G8B8?wa2::image::PixelOrder::bgra8:
            format==D3DFMT_X8R8G8B8?wa2::image::PixelOrder::bgrx8:format==D3DFMT_X8B8G8R8?wa2::image::PixelOrder::rgbx8:wa2::image::PixelOrder::rgba8;};
        const auto is32=[](D3DFORMAT format){return format==D3DFMT_A8R8G8B8 || format==D3DFMT_X8R8G8B8 || format==D3DFMT_A8B8G8R8 || format==D3DFMT_X8B8G8R8;};
        if(direct_rectangle && !indexed && !a[9] && !dst->gpu_dirty && valid(*direct_rectangle,dst->width,dst->height) &&
           U32(direct_rectangle->right-direct_rectangle->left)==width && U32(direct_rectangle->bottom-direct_rectangle->top)==height && is32(dst->format) &&
           (is32(D3DFORMAT(a[4])) || a[4]==D3DFMT_R8G8B8)){
            const auto dr=*direct_rectangle;
            const auto begin=std::chrono::steady_clock::now();
            auto output=std::span(dst->bytes).subspan((std::size_t(dr.top)*dst->width+dr.left)*4);
            if(wa2::graphics::transfer_source_pixels(p.memory,U32(start),a[5],size,order(D3DFORMAT(a[4])),output,std::size_t(dst->width)*4,order(dst->format),width,height)){
                ++copy_total.calls;copy_total.pixels+=std::uint64_t(width)*height;
                copy_total.total_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
                ++direct_source_copies;source_staging_bytes_avoided+=std::uint64_t(pitch)*height;
                dst->cpu_dirty=true;dst->gpu_dirty=false;++uploads;return 0;
            }
        }
        if(p.options.profile_execution)source_bytes_staged+=std::uint64_t(pitch)*height;
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
std::shared_ptr<wa2::graphics::MoviePresenter> install_wa2_shared_graphics(Process& p,std::shared_ptr<wa2::graphics::SurfaceHost> host){
    auto state=std::make_shared<Graphics>(p,std::move(host));state->install();
    p.state().shutdown.push_back([state]{state->shutdown();});p.state().reports["direct3d9"]=[state]{return state->report();};
    return state;
}
} // namespace wr
namespace wa2::graphics {std::shared_ptr<MoviePresenter> install(wr::Process& p,std::shared_ptr<SurfaceHost> host){return wr::install_wa2_shared_graphics(p,std::move(host));}}

