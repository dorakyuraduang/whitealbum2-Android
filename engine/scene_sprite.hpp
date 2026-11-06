#pragma once
#include "raster_image.hpp"
#include <memory>

namespace wa2::image {
enum class SpriteBlend {alpha,add};
enum class SpriteWorld {both,previous,current};
constexpr bool in_world(SpriteWorld world,bool previous)noexcept{return world==SpriteWorld::both || world==(previous?SpriteWorld::previous:SpriteWorld::current);}
struct Sprite {
    std::shared_ptr<const Raster> image;
    float x{},y{},width{},height{};
    float source_x{},source_y{},source_width{},source_height{};
    std::uint8_t red{255},green{255},blue{255},alpha{255};
    float angle{},pivot_x{},pivot_y{};
    std::uint32_t flip{};
    SpriteBlend blend{SpriteBlend::alpha};
    std::int32_t layer{},order{};
    SpriteWorld world{SpriteWorld::both};
};
}
