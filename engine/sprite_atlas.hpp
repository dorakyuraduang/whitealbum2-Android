#pragma once
#include "raster_image.hpp"
#include <memory>
#include <optional>

namespace wa2::image {
struct AtlasRegion {std::uint32_t x{},y{},width{},height{};};
struct SpriteAtlas {std::shared_ptr<const Raster> image;std::vector<AtlasRegion> regions;};
// One-pixel replicated gutters preserve clamped bilinear sampling. Input
// order/alpha/pixels are retained. An optional optimization: if packing cannot
// fit within the limit, callers keep their original textures and draw order.
std::optional<SpriteAtlas> make_sprite_atlas(std::span<const std::shared_ptr<const Raster>>,std::uint32_t maximum_side=2048);
}
