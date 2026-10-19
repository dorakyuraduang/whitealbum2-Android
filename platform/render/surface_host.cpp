#include "surface_host.hpp"
#include <algorithm>
namespace wa2::graphics {
std::optional<std::array<std::uint32_t,2>> SurfaceHost::backbuffer_size(std::uint32_t window,std::uint32_t width,std::uint32_t height,bool windowed) const {
    if(width && height)return std::array{width,height};if(!windowed)return {};
    const auto client=client_size(window);if((!width && client[0]<1) || (!height && client[1]<1))return {};
    return std::array{width?width:std::uint32_t(client[0]),height?height:std::uint32_t(client[1])};
}
ClientViewport SurfaceHost::client_viewport(std::uint32_t window,int width,int height) const {
    const auto size=client_size(window);
    return {size,size[0]>0 && size[1]>0?content_bounds(width,height,size[0],size[1]):std::array<int,4>{}};
}
std::array<int,4> SurfaceHost::content_bounds(int width,int height,std::uint32_t sw,std::uint32_t sh) const {
    if(width<=0 || height<=0 || !sw || !sh)return {};
    const double scale=std::min(double(width)/sw,double(height)/sh);
    const int w=std::max(1,int(sw*scale)),h=std::max(1,int(sh*scale));
    return {(width-w)/2,(height-h)/2,w,h};
}
}
