#pragma once
#include "raster_image.hpp"
#include "movie_reader.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>

struct SDL_Renderer;
struct SDL_Window;
namespace wa2::platform {
struct CanvasColour {std::uint8_t r{255},g{255},b{255},a{255};};
struct CanvasVertex {float x{},y{},u{},v{};CanvasColour colour;};
struct CanvasRect {int x{},y{},width{},height{};};
enum class CanvasBlend {copy,alpha,add,modulate};
enum class CanvasFilter {nearest,linear};
class CanvasTexture {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit CanvasTexture(std::unique_ptr<Impl>);
    friend class SdlCanvas;
public:
    ~CanvasTexture();
    CanvasTexture(const CanvasTexture&)=delete;
    CanvasTexture& operator=(const CanvasTexture&)=delete;
};
struct CanvasStatistics {std::uint64_t presents{},draws{},vertices{},uploads{};};
// Renderer/window ownership stays on the creating thread. Worker threads can
// release texture handles: destruction is deferred to the rendering thread.
// This backend provides textured 2-D triangles; game effect shaders remain a
// separate migration boundary, rather than being silently ignored.
class SdlCanvas {
    struct State;
    std::shared_ptr<State> state_;
    explicit SdlCanvas(SDL_Renderer* owned_renderer);
    friend struct CanvasTexture::Impl;
public:
    static std::unique_ptr<SdlCanvas> create(SDL_Window* window,bool vsync);
    static std::unique_ptr<SdlCanvas> adopt(SDL_Renderer* owned_renderer);
    ~SdlCanvas();
    SdlCanvas(const SdlCanvas&)=delete;
    SdlCanvas& operator=(const SdlCanvas&)=delete;
    void logical_size(int width,int height);
    std::shared_ptr<CanvasTexture> upload(const image::Raster&,CanvasFilter filter=CanvasFilter::linear);
    std::shared_ptr<CanvasTexture> upload_yuv(const movie::VideoFrame&);
    void update_yuv(const std::shared_ptr<CanvasTexture>&,const movie::VideoFrame&);
    void copy_yuv(const std::shared_ptr<CanvasTexture>&,const movie::VideoFrame&,float x,float y,float width,float height);
    void clear(CanvasColour colour);
    void clip(std::optional<CanvasRect> area);
    void triangles(const std::shared_ptr<CanvasTexture>& texture,std::span<const CanvasVertex> vertices,
                   CanvasBlend blend=CanvasBlend::alpha);
    std::shared_ptr<CanvasTexture> target(int width,int height);
    void render_to(const std::shared_ptr<CanvasTexture>& target,const std::function<void()>& draw);
    void masked(const std::shared_ptr<CanvasTexture>& image,const std::shared_ptr<CanvasTexture>& mask,
                const wa2::image::Raster& mask_pixels,float bias,float gain,bool flip_x,bool flip_y,bool invert);
    void reset_effects();
    void present(const std::function<void()>& submitted={});
    CanvasStatistics statistics() const;
};
}
