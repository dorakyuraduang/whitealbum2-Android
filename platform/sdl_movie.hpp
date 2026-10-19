#pragma once
#include "sdl_canvas.hpp"

namespace wa2::platform{
class SdlMovie{
    SdlCanvas& canvas_;
    std::shared_ptr<CanvasTexture> texture_;
    std::shared_ptr<const void> uploaded_;
    int width_{},height_{};
public:
    explicit SdlMovie(SdlCanvas& canvas):canvas_(canvas){}
    void draw(const movie::VideoFrame& frame,int logical_width=1280,int logical_height=720);
    void reset(){texture_.reset();uploaded_.reset();width_=height_=0;}
};
}
