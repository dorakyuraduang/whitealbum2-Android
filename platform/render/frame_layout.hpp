#pragma once
#include "surface_host.hpp"
#include <optional>
namespace wa2::graphics {
using FrameRect=std::array<int,4>; // left, top, right, bottom
struct FrameLayout {FrameRect source,destination;};
// Source rectangles address backbuffer pixels. Destination rectangles address
// the original window CLIENT area, even when the backbuffer has another size.
std::optional<FrameLayout> layout_frame(std::uint32_t width,std::uint32_t height,
    const FrameRect& source,const std::optional<FrameRect>& destination,const ClientViewport&);
}
