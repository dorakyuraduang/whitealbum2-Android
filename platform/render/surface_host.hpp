#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
struct SDL_Window;
namespace wr {class Process;}
namespace wa2::subtitles {class Playback;}
namespace wa2::graphics {
class MoviePresenter;
struct ClientViewport {
    std::array<int,2> size{};
    std::array<int,4> drawable{};
};
// The window/input adapter owns the SDL window. The shared graphics backend
// owns its GL context and all source D3D resources. No host pointer enters RAM.
class SurfaceHost {
public:
    virtual ~SurfaceHost()=default;
    virtual SDL_Window* acquire(std::uint32_t source_window,std::uint32_t width,
                                std::uint32_t height,bool windowed)=0;
    virtual bool embedded_gl() const noexcept=0;
    virtual void source_cursor(std::int32_t x,std::int32_t y)=0;
    virtual int cursor_visible(bool)=0;
    virtual std::array<int,2> client_size(std::uint32_t source_window) const=0;
    // Native movie rectangles use source window client pixels. They must not
    // be scaled as D3D backbuffer pixels when the two resolutions differ.
    ClientViewport client_viewport(std::uint32_t source_window,int drawable_width,int drawable_height) const;
    std::optional<std::array<std::uint32_t,2>> backbuffer_size(std::uint32_t source_window,
        std::uint32_t width,std::uint32_t height,bool windowed) const;
    // Physical drawable bounds, in top-left coordinates. This is also the
    // rectangle used by the input adapter to map touch/mouse into source pixels.
    virtual std::array<int,4> content_bounds(int width,int height,
                                            std::uint32_t source_width,std::uint32_t source_height) const;
    virtual void frame_submitted()=0;
};
std::shared_ptr<MoviePresenter> install(wr::Process&,std::shared_ptr<SurfaceHost>,std::shared_ptr<subtitles::Playback> subtitles={});
}
