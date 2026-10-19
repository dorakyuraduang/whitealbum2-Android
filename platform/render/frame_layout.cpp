#include "frame_layout.hpp"
namespace wa2::graphics {
std::optional<FrameLayout> layout_frame(std::uint32_t width,std::uint32_t height,
    const FrameRect& source,const std::optional<FrameRect>& destination,const ClientViewport& viewport){
    const auto valid=[](const FrameRect& r,std::int64_t w,std::int64_t h){
        return r[0]>=0 && r[1]>=0 && r[2]>r[0] && r[3]>r[1] && r[2]<=w && r[3]<=h;
    };
    const auto& size=viewport.size;const auto& bounds=viewport.drawable;
    if(!valid(source,width,height) || size[0]<1 || size[1]<1 || bounds[2]<1 || bounds[3]<1)return {};
    const auto dr=destination.value_or(FrameRect{0,0,size[0],size[1]});if(!valid(dr,size[0],size[1]))return {};
    FrameRect output;
    for(unsigned n=0;n<4;++n){const unsigned axis=n%2;output[n]=bounds[axis]+int(std::int64_t(dr[n])*bounds[axis+2]/size[axis]);}
    return FrameLayout{source,output};
}
}
