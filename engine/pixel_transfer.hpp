#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
namespace wa2::image {
enum class PixelOrder:unsigned {rgba8,bgra8,rgbx8,bgrx8};
// Native byte transfer, shared by every graphics host. Row padding is never
// written. X channels become opaque alpha; overlapping rectangles are safe.
void transfer_32(std::span<std::uint8_t> destination,std::size_t destination_pitch,PixelOrder destination_order,
    std::span<const std::uint8_t> source,std::size_t source_pitch,PixelOrder source_order,
    std::uint32_t width,std::uint32_t height);
}
