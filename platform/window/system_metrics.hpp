#pragma once
#include <cstdint>
#include <optional>
namespace wa2::window {
// The common fullscreen surface has no native window/menu decorations. Hosts
// with native widgets can override these values through the Host interface.
inline std::optional<std::int32_t> surface_metric(std::int32_t index,std::int32_t x,std::int32_t y,std::int32_t width,std::int32_t height){
    switch(index){
    case 0:case 16:case 59:case 61:case 78:return width;
    case 1:case 17:case 60:case 62:case 79:return height;
    case 2:case 3:case 4:case 5:case 6:case 7:case 8:case 15:case 23:case 32:case 33:case 54:case 55:return 0;
    case 19:case 43:case 80:case 81:return 1;
    case 76:return x;case 77:return y;
    default:return std::nullopt;
    }
}
}
