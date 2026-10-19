#pragma once
#include "sdl_canvas.hpp"
#include "scene_background.hpp"
#include "scene_sprite.hpp"
#include <vector>

namespace wa2::platform {
// Texture lifetimes follow live scene images. Retiring a background releases
// its upload, rather than retaining every image visited during fast-forward.
class SdlScene {
    struct Upload {std::weak_ptr<const image::Raster> image;std::shared_ptr<CanvasTexture> texture;};
    SdlCanvas& canvas_;
    std::vector<Upload> uploads_;
    std::optional<script::BackgroundLayer> mask_source_;
    std::shared_ptr<CanvasTexture> mask_target_;
    std::vector<CanvasVertex> sprite_vertices_;
    std::shared_ptr<CanvasTexture> texture(const std::shared_ptr<const image::Raster>& image);
    void draw(const script::BackgroundLayer& layer);
public:
    explicit SdlScene(SdlCanvas& canvas):canvas_(canvas){}
    void background(const script::BackgroundFrame& frame,std::span<const image::Sprite> environment={});
    void sprites(std::span<const image::Sprite> items);
    // The host calls this on SDL_RENDER_DEVICE_RESET before re-uploading.
    void reset_textures(){uploads_.clear();mask_source_.reset();mask_target_.reset();canvas_.reset_effects();}
    std::size_t texture_count()const noexcept{return uploads_.size();}
};
}
