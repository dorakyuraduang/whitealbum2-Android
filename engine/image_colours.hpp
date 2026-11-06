#pragma once
#include "raster_image.hpp"
#include <memory>

namespace wa2::image {
struct ColourMap {std::array<std::array<std::uint8_t,256>,3> channels;};
// Original AMP files contain one shared table, three RGB tables, or a final
// shared table followed by three RGB tables and an unused fourth channel.
ColourMap parse_colour_map(std::span<const std::uint8_t> bytes);
struct ImageStyle {
    std::int32_t chroma{256};
    std::shared_ptr<const ColourMap> colours;
};
void validate_image_style(const ImageStyle&);
// Conversion runs once while loading owned RGBA pixels, before GPU upload.
// Alpha is retained. Chroma precedes the optional per-channel AMP lookups.
void apply_image_style(Raster&,const ImageStyle&);
}
