#pragma once
#include "frame_layout.hpp"
#include <algorithm>

namespace wa2::graphics {
struct SubtitleProjection {
    std::array<int,2> drawable;
    FrameRect clip;
    std::array<float,2> origin;
    float scale;
    std::array<float,4> position(float x,float y) const {
        return {(origin[0]+x*scale)*2/drawable[0]-1,
                1-(origin[1]+y*scale)*2/drawable[1],0,1};
    }
};
// Captions are a presentation overlay, on a complete 1280x720 canvas. The
// source Present rectangle may show only part of a larger backbuffer; neither
// that allocation nor its crop can change the caption size or centre.
inline std::optional<SubtitleProjection> subtitle_projection(int width,int height,const FrameRect& destination){
    if(width<1 || height<1 || destination[0]<0 || destination[1]<0 ||
       destination[2]<=destination[0] || destination[3]<=destination[1] ||
       destination[2]>width || destination[3]>height)return {};
    const auto w=float(destination[2]-destination[0]),h=float(destination[3]-destination[1]);
    const auto scale=std::min(w/1280.f,h/720.f);
    return SubtitleProjection{{width,height},destination,
        {destination[0]+(w-1280.f*scale)/2,destination[1]+(h-720.f*scale)/2},scale};
}
}
