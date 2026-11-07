#pragma once
#include "raster_image.hpp"
#include <memory>

namespace wa2::image {
enum class SpriteBlend {alpha,add,screen,multiply};
// Screen/multiply also change the destination at zero source alpha. Only
// ordinary alpha/additive sprites can be culled on alpha alone.
constexpr bool contributes(SpriteBlend blend,std::uint8_t alpha)noexcept{return alpha || blend==SpriteBlend::screen || blend==SpriteBlend::multiply;}
enum class SpriteWorld {both,previous,current};
constexpr bool in_world(SpriteWorld world,bool previous)noexcept{return world==SpriteWorld::both || world==(previous?SpriteWorld::previous:SpriteWorld::current);}
// Screen overlays and the independently shaken message window share one
// layer order. Keeping the space on each item lets a foreground image pass
// between them without transforming fixed UI or splitting the game by host.
enum class SpriteSpace {screen,dialogue};
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
    SpriteSpace space{SpriteSpace::screen};
};
}
