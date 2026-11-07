#include "scene_geometry.hpp"
#include "scalar_math.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wa2::image {
std::optional<std::array<float,2>> scene_point(float x,float y,const SceneTransform& transform){
    for(auto value:{x,y,transform.x,transform.y,transform.scale,transform.angle})if(!std::isfinite(value) || std::abs(value)>1.e9f)throw std::runtime_error("Native input transform out of range");
    if(transform.scale<0)throw std::runtime_error("Invalid native input scale");if(!transform.scale)return {};
    float cosine=1,sine=0;if(transform.angle){const auto phase=float(double(transform.angle)*.01745329238474369);cosine=float(math::cosine_finite(phase).value);sine=float(math::sine_finite(phase).value);}
    const auto dx=(x-640)/transform.scale,dy=(y-360)/transform.scale;
    const std::array point{cosine*dx-sine*dy+640-transform.x,sine*dx+cosine*dy+360-transform.y};
    if(!std::isfinite(point[0]) || !std::isfinite(point[1]))throw std::runtime_error("Native input transform overflow");return point;
}
std::optional<std::array<QuadVertex,6>> scene_quad(const QuadGeometry& q,std::uint32_t image_width,std::uint32_t image_height){
    for(const auto value:{q.x,q.y,q.width,q.height,q.source_x,q.source_y,q.source_width,q.source_height,q.angle,q.pivot_x,q.pivot_y,q.scale_x,q.scale_y,q.outer.x,q.outer.y,q.outer.scale,q.outer.angle})
        if(!std::isfinite(value) || std::abs(value)>1.e9f)throw std::runtime_error("Native quad coordinate out of range");
    if(!image_width || !image_height || image_width>65535 || image_height>65535 || q.width<0 || q.height<0 ||
       q.source_width<0 || q.source_height<0 || q.scale_x<0 || q.scale_y<0 || q.outer.scale<0 || (q.flip&~3u))throw std::runtime_error("Invalid native quad dimensions or reflection");
    if(!q.width || !q.height || !q.source_width || !q.source_height || !q.scale_x || !q.scale_y || !q.outer.scale)return {};
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
    if(!q.outer.identity()){
        cosine=1;sine=0;if(q.outer.angle){const auto phase=float(double(q.outer.angle)*.01745329238474369);cosine=float(math::cosine_finite(phase).value);sine=float(math::sine_finite(phase).value);}
        for(auto& vertex:vertices){const auto dx=vertex.x+q.outer.x-640,dy=vertex.y+q.outer.y-360;
            vertex.x=640+(cosine*dx+sine*dy)*q.outer.scale;vertex.y=360+(cosine*dy-sine*dx)*q.outer.scale;
            if(!std::isfinite(vertex.x) || !std::isfinite(vertex.y))throw std::runtime_error("Native scene transform overflow");}
    }
    return vertices;
}
}
