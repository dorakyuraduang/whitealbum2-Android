#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace wa2::image {
struct QuadVertex {float x{},y{},u{},v{};};
struct SceneTransform {
    float x{},y{},scale{1},angle{};
    bool identity()const noexcept{return x==0 && y==0 && scale==1 && angle==0;}
    bool operator==(const SceneTransform&)const=default;
};
struct QuadGeometry {
    float x{},y{},width{},height{};
    float source_x{},source_y{},source_width{},source_height{};
    float angle{},pivot_x{},pivot_y{},scale_x{1},scale_y{1};
    std::uint32_t flip{};
    SceneTransform outer;
};
// Available pixels are clipped in source space before transforming the quad.
// Native textures have no allocation padding. Position/UV clipping therefore
// omits the original texture's unused transparent cells, without stretching
// the remaining image. Positive angles turn towards decreasing screen Y;
// reflection precedes rotation, followed by the independent X/Y scales.
// SDL uses pixel edges directly, so no D3D9 half-pixel offset is applied here.
std::optional<std::array<QuadVertex,6>> scene_quad(const QuadGeometry&,std::uint32_t image_width,std::uint32_t image_height);
}
