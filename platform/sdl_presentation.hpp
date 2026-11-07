#pragma once
#include "render_frame.hpp"
#include "sdl_scene.hpp"
#include "sdl_movie.hpp"

namespace wa2::platform {
// The same adapter consumes shared native frames on desktop and Android.
// Window/device/event creation and SDL_RenderPresent remain host-owned.
class SdlPresentation {
    SdlCanvas& canvas_;
    SdlScene scene_;
    SdlMovie movie_;
    bool movie_active_{};
public:
    explicit SdlPresentation(SdlCanvas& canvas):canvas_(canvas),scene_(canvas),movie_(canvas){}
    void draw(const game::RenderFrame&);
    void reset_textures(){scene_.reset_textures();movie_.reset();}
    std::size_t texture_count()const noexcept{return scene_.texture_count();}
};
}
