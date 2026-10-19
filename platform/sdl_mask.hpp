#pragma once
#include <memory>
struct SDL_Texture;
struct SDL_Renderer;

namespace wa2::platform {
// One GLES2 pass implements the original pf00..pf03 alpha-mask shaders.
// The SDL renderer owns the context and textures. No readback is used here.
class SdlMaskProgram {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    SdlMaskProgram();
    ~SdlMaskProgram();
    void draw(SDL_Renderer* renderer,SDL_Texture* image,SDL_Texture* mask,float bias,float gain,
              bool flip_x,bool flip_y,bool invert);
    void abandon() noexcept;
};
}
