#include "scene_geometry.hpp"
#include "scalar_math.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wa2::image {
std::optional<std::array<QuadVertex,6>> scene_quad(const QuadGeometry& q,std::uint32_t image_width,std::uint32_t image_height){
    for(const auto value:{q.x,q.y,q.width,q.height,q.source_x,q.source_y,q.source_width,q.source_height,q.angle,q.pivot_x,q.pivot_y,q.scale_x,q.scale_y})
        if(!std::isfinite(value) || std::abs(value)>1.e9f)throw std::runtime_error("Native quad coordinate out of range");
    if(!image_width || !image_height || image_width>65535 || image_height>65535 || q.width<0 || q.height<0 ||
       q.source_width<0 || q.source_height<0 || q.scale_x<0 || q.scale_y<0 || (q.flip&~3u))throw std::runtime_error("Invalid native quad dimensions or reflection");
    if(!q.width || !q.height || !q.source_width || !q.source_height || !q.scale_x || !q.scale_y)return {};
    const auto left=std::max(0.f,q.source_x),top=std::max(0.f,q.source_y),
        right=std::min(float(image_width),q.source_x+q.source_width),bottom=std::min(float(image_height),q.source_y+q.source_height);
    if(left>=right || top>=bottom)return {};
    const auto x=q.x+(left-q.source_x)/q.source_width*q.width,y=q.y+(top-q.source_y)/q.source_height*q.height,
        x1=q.x+(right-q.source_x)/q.source_width*q.width,y1=q.y+(bottom-q.source_y)/q.source_height*q.height;
    const auto u=left/image_width,v=top/image_height,u1=right/image_width,v1=bottom/image_height;
    std::array<QuadVertex,6> vertices{{{x,y,u,v},{x1,y,u1,v},{x1,y1,u1,v1},{x,y,u,v},{x1,y1,u1,v1},{x,y1,u,v1}}};
    float cosine=1,sine=0;
    if(q.angle){
        // The cached sprite routine rounds radians to binary32 before its
        // ordinary CRT sine/cosine calls. This common math has no CPU state.
        const auto phase=float(double(q.angle)*.01745329238474369);
        cosine=float(math::cosine_finite(phase).value);sine=float(math::sine_finite(phase).value);
    }
    for(auto& vertex:vertices){
        auto dx=vertex.x-q.pivot_x,dy=vertex.y-q.pivot_y;
        if(q.flip&1)dx=-dx;if(q.flip&2)dy=-dy;
        vertex.x=q.pivot_x+(cosine*dx+sine*dy)*q.scale_x;
        vertex.y=q.pivot_y+(cosine*dy-sine*dx)*q.scale_y;
        if(!std::isfinite(vertex.x) || !std::isfinite(vertex.y))throw std::runtime_error("Native quad transform overflow");
    }
    return vertices;
}
}
