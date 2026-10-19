#define SDL_MAIN_HANDLED
#include "sdl_canvas.hpp"
#include "sdl_mask.hpp"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace wa2::platform {
namespace {
[[noreturn]] void fail(const char* operation){throw std::runtime_error(std::string(operation)+": "+SDL_GetError());}
SDL_BlendMode blend_mode(CanvasBlend blend){
    switch(blend){case CanvasBlend::copy:return SDL_BLENDMODE_NONE;case CanvasBlend::alpha:return SDL_BLENDMODE_BLEND;
    case CanvasBlend::add:return SDL_BLENDMODE_ADD;case CanvasBlend::modulate:return SDL_BLENDMODE_MOD;}
    throw std::runtime_error("invalid native canvas blend mode");
}
}
struct SdlCanvas::State {
    SDL_Renderer* renderer{};
    const std::thread::id owner{std::this_thread::get_id()};
    std::mutex mutex;
    std::vector<SDL_Texture*> retired;
    std::vector<SDL_Vertex> vertices;
    CanvasStatistics statistics;
    std::unique_ptr<SdlMaskProgram> mask_program;
    SDL_Texture* memory_mask_texture{};
    int memory_mask_width{},memory_mask_height{};
    std::vector<std::uint8_t> memory_mask_pixels;
    bool gles{},memory_only{};
    void enter(){
        if(std::this_thread::get_id()!=owner)throw std::runtime_error("native canvas called from a foreign thread");
        if(!renderer)throw std::runtime_error("native canvas is closed");
        std::vector<SDL_Texture*> pending;
        {std::lock_guard lock(mutex);pending.swap(retired);}
        for(auto* texture:pending)SDL_DestroyTexture(texture);
    }
    void release(SDL_Texture* texture){
        std::lock_guard lock(mutex);
        if(!renderer)return; // SDL_DestroyRenderer already released all textures.
        if(std::this_thread::get_id()==owner)SDL_DestroyTexture(texture);
        else retired.push_back(texture);
    }
};
struct CanvasTexture::Impl {
    std::weak_ptr<SdlCanvas::State> owner;
    SDL_Texture* texture{};
    int width{},height{};
    bool yuv{};
    bool render_target{};
    std::vector<std::uint8_t> memory_pixels;
    ~Impl(){if(auto state=owner.lock())state->release(texture);}
};
CanvasTexture::CanvasTexture(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
CanvasTexture::~CanvasTexture()=default;
SdlCanvas::SdlCanvas(SDL_Renderer* renderer):state_(std::make_shared<State>()){
    if(!renderer)throw std::runtime_error("missing native canvas renderer");state_->renderer=renderer;
    SDL_RendererInfo info{};if(SDL_GetRendererInfo(renderer,&info))fail("SDL_GetRendererInfo");
    state_->gles=info.name && std::string_view(info.name)=="opengles2";
    state_->memory_only=(info.flags&SDL_RENDERER_SOFTWARE) && !SDL_RenderGetWindow(renderer);
}
std::unique_ptr<SdlCanvas> SdlCanvas::create(SDL_Window* window,bool vsync){
    if(!window)throw std::runtime_error("missing native canvas window");
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_RENDER_DRIVER,"opengles2");
#endif
    auto* renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|(vsync?SDL_RENDERER_PRESENTVSYNC:0));
    if(!renderer)fail("SDL_CreateRenderer");
    auto result=adopt(renderer);
    // Compile once while constructing the graphics backend, before a script
    // starts its timed fade. The first transition must not compile a shader.
    if(result->state_->gles)result->state_->mask_program=std::make_unique<SdlMaskProgram>();
    return result;
}
std::unique_ptr<SdlCanvas> SdlCanvas::adopt(SDL_Renderer* renderer){
    if(!renderer)throw std::runtime_error("missing native canvas renderer");
    try{return std::unique_ptr<SdlCanvas>(new SdlCanvas(renderer));}
    catch(...){SDL_DestroyRenderer(renderer);throw;}
}
SdlCanvas::~SdlCanvas(){
    // The canvas itself belongs to the render loop. Texture handles can outlive
    // it; SDL releases those handles here and their later destructors are inert.
    state_->mask_program.reset();
    std::lock_guard lock(state_->mutex);auto* renderer=state_->renderer;state_->renderer=nullptr;state_->retired.clear();
    if(renderer)SDL_DestroyRenderer(renderer);
}
void SdlCanvas::logical_size(int width,int height){
    state_->enter();if(width<1 || height<1)throw std::runtime_error("invalid native canvas size");
    if(SDL_RenderSetLogicalSize(state_->renderer,width,height))fail("SDL_RenderSetLogicalSize");
}
std::shared_ptr<CanvasTexture> SdlCanvas::upload(const image::Raster& image,CanvasFilter filter){
    state_->enter();
    if(!image.width || !image.height || image.width>std::uint32_t(std::numeric_limits<int>::max()/4) ||
       image.height>std::uint32_t(std::numeric_limits<int>::max()) ||
       std::uint64_t(image.width)*image.height*4!=image.rgba.size())throw std::runtime_error("invalid native texture pixels");
    auto impl=std::make_unique<CanvasTexture::Impl>();impl->owner=state_;
    impl->width=int(image.width);impl->height=int(image.height);
    impl->texture=SDL_CreateTexture(state_->renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,int(image.width),int(image.height));
    if(!impl->texture)fail("SDL_CreateTexture");
    if(SDL_UpdateTexture(impl->texture,nullptr,image.rgba.data(),int(image.width*4)))fail("SDL_UpdateTexture");
    if(SDL_SetTextureScaleMode(impl->texture,filter==CanvasFilter::nearest?SDL_ScaleModeNearest:SDL_ScaleModeLinear))fail("SDL_SetTextureScaleMode");
    ++state_->statistics.uploads;return std::shared_ptr<CanvasTexture>(new CanvasTexture(std::move(impl)));
}
void SdlCanvas::clear(CanvasColour colour){
    state_->enter();if(SDL_SetRenderDrawColor(state_->renderer,colour.r,colour.g,colour.b,colour.a))fail("SDL_SetRenderDrawColor");
    if(SDL_RenderClear(state_->renderer))fail("SDL_RenderClear");
}
void SdlCanvas::clip(std::optional<CanvasRect> area){
    state_->enter();SDL_Rect rectangle{};
    if(area){if(area->width<0 || area->height<0)throw std::runtime_error("invalid native clipping rectangle");rectangle={area->x,area->y,area->width,area->height};}
    if(SDL_RenderSetClipRect(state_->renderer,area?&rectangle:nullptr))fail("SDL_RenderSetClipRect");
}
void SdlCanvas::triangles(const std::shared_ptr<CanvasTexture>& texture,std::span<const CanvasVertex> vertices,CanvasBlend blend){
    state_->enter();
    if(vertices.size()%3 || vertices.size()>1000000)throw std::runtime_error("invalid native triangle batch");
    if(vertices.empty())return;
    SDL_Texture* handle{};
    if(texture){if(texture->impl_->owner.lock()!=state_)throw std::runtime_error("native texture belongs to a different canvas");handle=texture->impl_->texture;}
    const auto mode=blend_mode(blend);
    if(handle){if(SDL_SetTextureBlendMode(handle,mode))fail("SDL_SetTextureBlendMode");}
    else if(SDL_SetRenderDrawBlendMode(state_->renderer,mode))fail("SDL_SetRenderDrawBlendMode");
    auto& output=state_->vertices;output.resize(vertices.size());
    for(std::size_t i=0;i<vertices.size();++i){const auto& value=vertices[i];
        if(!std::isfinite(value.x)||!std::isfinite(value.y)||!std::isfinite(value.u)||!std::isfinite(value.v))throw std::runtime_error("nonfinite native vertex");
        output[i]={{value.x,value.y},{value.colour.r,value.colour.g,value.colour.b,value.colour.a},{value.u,value.v}};
    }
    // SDL's software quad shortcut forces untextured rectangles to ordinary
    // alpha blending. Submit separate triangles for the owned-memory reference
    // renderer so ADD/copy/modulate retain their requested equations. GLES
    // retains the single batched submission used by the Android game.
    if(!handle && state_->memory_only && blend!=CanvasBlend::alpha){
        for(std::size_t n=0;n<output.size();n+=3)if(SDL_RenderGeometry(state_->renderer,nullptr,output.data()+n,3,nullptr,0))fail("SDL_RenderGeometry reference blend");
    }else if(SDL_RenderGeometry(state_->renderer,handle,output.data(),int(output.size()),nullptr,0))fail("SDL_RenderGeometry");
    ++state_->statistics.draws;state_->statistics.vertices+=vertices.size();
}
void SdlCanvas::present(const std::function<void()>& submitted){
    state_->enter();SDL_RenderPresent(state_->renderer);++state_->statistics.presents;if(submitted)submitted();
}
CanvasStatistics SdlCanvas::statistics() const {
    if(std::this_thread::get_id()!=state_->owner)throw std::runtime_error("native canvas statistics read from a foreign thread");
    return state_->statistics;
}
std::shared_ptr<CanvasTexture> SdlCanvas::target(int width,int height){
    state_->enter();if(width<1 || height<1 || std::uint64_t(width)*height>128ull*1024*1024/4)
        throw std::runtime_error("invalid native render target size");
    auto impl=std::make_unique<CanvasTexture::Impl>();impl->owner=state_;impl->width=width;impl->height=height;impl->render_target=true;
    impl->texture=SDL_CreateTexture(state_->renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,width,height);
    if(!impl->texture)fail("SDL_CreateTexture target");
    if(SDL_SetTextureScaleMode(impl->texture,SDL_ScaleModeLinear))fail("SDL_SetTextureScaleMode target");
    return std::shared_ptr<CanvasTexture>(new CanvasTexture(std::move(impl)));
}
void SdlCanvas::render_to(const std::shared_ptr<CanvasTexture>& texture,const std::function<void()>& draw){
    state_->enter();if(!texture || texture->impl_->owner.lock()!=state_ || !texture->impl_->render_target || !draw)
        throw std::runtime_error("invalid native render target");
    auto* renderer=state_->renderer;auto* previous=SDL_GetRenderTarget(renderer);
    if(previous)throw std::runtime_error("nested native render targets are not implemented");
    if(SDL_SetRenderTarget(renderer,texture->impl_->texture))fail("SDL_SetRenderTarget");
    try{
        draw();
        if(state_->memory_only){
            // Reference backend for tests on SDL_CreateSoftwareRenderer's
            // owned memory surfaces ONLY. A renderer with a window cannot
            // enter this branch. Android never reads GPU pixels back.
            auto& impl=*texture->impl_;impl.memory_pixels.resize(std::size_t(impl.width)*impl.height*4);
            if(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,impl.memory_pixels.data(),impl.width*4))fail("read owned test render target");
        }
    }catch(...){SDL_SetRenderTarget(renderer,previous);throw;}
    if(SDL_SetRenderTarget(renderer,previous))fail("restore native render target");
}
void SdlCanvas::masked(const std::shared_ptr<CanvasTexture>& image,const std::shared_ptr<CanvasTexture>& mask,
        const wa2::image::Raster& pixels,float bias,float gain,bool flip_x,bool flip_y,bool invert){
    state_->enter();if(!image || !mask || image->impl_->owner.lock()!=state_ || mask->impl_->owner.lock()!=state_ ||
        !image->impl_->render_target || !std::isfinite(bias) || !std::isfinite(gain) || gain<=0 || !pixels.width || !pixels.height ||
        std::uint64_t(pixels.width)*pixels.height*4!=pixels.rgba.size() || mask->impl_->yuv ||
        mask->impl_->width!=int(pixels.width) || mask->impl_->height!=int(pixels.height))throw std::runtime_error("invalid native mask draw");
    auto* renderer=state_->renderer;
    if(SDL_GetRenderTarget(renderer)==image->impl_->texture || SDL_GetRenderTarget(renderer)==mask->impl_->texture)
        throw std::runtime_error("native mask cannot sample the active render target");
    if(state_->gles){
        // Flush through a harmless transparent primitive so SDL applies its
        // deferred viewport/clip before the native pass. Reconstructing the
        // physical viewport from RenderGetViewport loses a pixel when a
        // letterbox offset is not a multiple of the logical render scale.
        if(SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND) || SDL_SetRenderDrawColor(renderer,0,0,0,0) ||
           SDL_RenderDrawPointF(renderer,0,0) || SDL_RenderFlush(renderer))fail("prepare native mask viewport");
        if(!state_->mask_program){
            if(SDL_GL_BindTexture(image->impl_->texture,nullptr,nullptr))fail("activate native mask context");
            SDL_GL_UnbindTexture(image->impl_->texture);state_->mask_program=std::make_unique<SdlMaskProgram>();
        }
        state_->mask_program->draw(renderer,image->impl_->texture,mask->impl_->texture,bias,gain,flip_x,flip_y,invert);
    }else if(state_->memory_only){
        if(SDL_RenderFlush(renderer))fail("SDL_RenderFlush owned test mask");
        const auto& source=*image->impl_;auto& output=state_->memory_mask_pixels;
        if(source.memory_pixels.size()!=std::size_t(source.width)*source.height*4)throw std::runtime_error("missing owned test target pixels");
        output=source.memory_pixels;
        const auto sample=[&](float x,float y){
            const auto x0=int(std::floor(x)),y0=int(std::floor(y));const auto fx=x-x0,fy=y-y0;
            const auto at=[&](int xx,int yy){return float(pixels.rgba[(std::size_t(std::clamp(yy,0,int(pixels.height)-1))*pixels.width+
                std::clamp(xx,0,int(pixels.width)-1))*4+3])*(1.f/255.f);};
            return (at(x0,y0)*(1-fx)+at(x0+1,y0)*fx)*(1-fy)+(at(x0,y0+1)*(1-fx)+at(x0+1,y0+1)*fx)*fy;
        };
        for(int y=0;y<source.height;++y)for(int x=0;x<source.width;++x){
            auto u=(float(x)+.5f)/source.width,v=(float(y)+.5f)/source.height;
            if(flip_x)u=1-u;if(flip_y)v=1-v;
            auto alpha=sample(u*pixels.width-.5f,v*pixels.height-.5f);if(invert)alpha=1-alpha;
            output[(std::size_t(y)*source.width+x)*4+3]=std::uint8_t(std::lround(std::clamp((alpha+bias)*gain,0.f,1.f)*255));
        }
        if(state_->memory_mask_width!=source.width || state_->memory_mask_height!=source.height){
            if(state_->memory_mask_texture)SDL_DestroyTexture(state_->memory_mask_texture);
            state_->memory_mask_texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,source.width,source.height);
            if(!state_->memory_mask_texture)fail("create owned test mask texture");
            state_->memory_mask_width=source.width;state_->memory_mask_height=source.height;
        }
        if(SDL_UpdateTexture(state_->memory_mask_texture,nullptr,output.data(),source.width*4) ||
           SDL_SetTextureBlendMode(state_->memory_mask_texture,SDL_BLENDMODE_BLEND))fail("update owned test mask texture");
        const SDL_FRect rectangle{0,0,float(source.width),float(source.height)};
        if(SDL_RenderCopyF(renderer,state_->memory_mask_texture,nullptr,&rectangle))fail("draw owned test mask texture");
    }else throw std::runtime_error("native mask shaders require the GLES2 rendering backend");
    ++state_->statistics.draws;state_->statistics.vertices+=4;
}
void SdlCanvas::reset_effects(){
    state_->enter();if(state_->mask_program){state_->mask_program->abandon();state_->mask_program.reset();}
    // Renderer reset invalidates its old texture objects, including this one.
    if(state_->memory_mask_texture)SDL_DestroyTexture(state_->memory_mask_texture);
    state_->memory_mask_texture=nullptr;state_->memory_mask_width=state_->memory_mask_height=0;
}
namespace{
struct YuvMode{
    SDL_YUV_CONVERSION_MODE previous{SDL_GetYUVConversionMode()};
    explicit YuvMode(const movie::VideoFrame& frame){
        SDL_SetYUVConversionMode(frame.full_range?SDL_YUV_CONVERSION_JPEG:
            (frame.matrix==movie::ColourMatrix::bt709?SDL_YUV_CONVERSION_BT709:SDL_YUV_CONVERSION_BT601));
    }
    ~YuvMode(){SDL_SetYUVConversionMode(previous);}
};
void validate_yuv(const movie::VideoFrame& frame){
    if(!frame.owner || frame.width<1 || frame.height<1 || frame.width>8192 || frame.height>8192)
        throw std::runtime_error("invalid native YUV movie dimensions");
    for(unsigned plane=0;plane<3;++plane){const auto& value=frame.planes[plane];
        const auto width=plane?(frame.width+1)/2:frame.width,height=plane?(frame.height+1)/2:frame.height;
        if(!value.pixels || value.pitch<width || value.width!=width || value.height!=height)
            throw std::runtime_error("invalid native YUV movie plane");
    }
}
}
std::shared_ptr<CanvasTexture> SdlCanvas::upload_yuv(const movie::VideoFrame& frame){
    state_->enter();validate_yuv(frame);YuvMode mode(frame);
    auto impl=std::make_unique<CanvasTexture::Impl>();impl->owner=state_;
    impl->width=frame.width;impl->height=frame.height;impl->yuv=true;
    impl->texture=SDL_CreateTexture(state_->renderer,SDL_PIXELFORMAT_IYUV,SDL_TEXTUREACCESS_STREAMING,frame.width,frame.height);
    if(!impl->texture)fail("SDL_CreateTexture YUV");
    if(SDL_SetTextureScaleMode(impl->texture,SDL_ScaleModeLinear))fail("SDL_SetTextureScaleMode YUV");
    auto texture=std::shared_ptr<CanvasTexture>(new CanvasTexture(std::move(impl)));update_yuv(texture,frame);return texture;
}
void SdlCanvas::update_yuv(const std::shared_ptr<CanvasTexture>& texture,const movie::VideoFrame& frame){
    state_->enter();validate_yuv(frame);
    if(!texture || texture->impl_->owner.lock()!=state_ || !texture->impl_->yuv ||
       texture->impl_->width!=frame.width || texture->impl_->height!=frame.height)
        throw std::runtime_error("native YUV texture belongs to another movie or canvas");
    YuvMode mode(frame);
    if(SDL_UpdateYUVTexture(texture->impl_->texture,nullptr,frame.planes[0].pixels,frame.planes[0].pitch,
        frame.planes[1].pixels,frame.planes[1].pitch,frame.planes[2].pixels,frame.planes[2].pitch))fail("SDL_UpdateYUVTexture");
    ++state_->statistics.uploads;
}
void SdlCanvas::copy_yuv(const std::shared_ptr<CanvasTexture>& texture,const movie::VideoFrame& frame,
        float x,float y,float width,float height){
    state_->enter();
    if(!texture || texture->impl_->owner.lock()!=state_ || !texture->impl_->yuv ||
       texture->impl_->width!=frame.width || texture->impl_->height!=frame.height)
        throw std::runtime_error("invalid native YUV draw texture");
    if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0)
        throw std::runtime_error("invalid native YUV draw bounds");
    const SDL_FRect destination{x,y,width,height};YuvMode mode(frame);
    if(SDL_SetTextureBlendMode(texture->impl_->texture,SDL_BLENDMODE_NONE) ||
       SDL_RenderCopyF(state_->renderer,texture->impl_->texture,nullptr,&destination) || SDL_RenderFlush(state_->renderer))
        fail("SDL_RenderCopyF YUV");
    ++state_->statistics.draws;
}
}
