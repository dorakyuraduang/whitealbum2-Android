#pragma once
#include "scene_sprite.hpp"
#include <span>
#include <string_view>

namespace wa2::game {
// Cached 0x442994 maps ASCII from 0x20 in sixteen columns. The save/date
// sheet has 12x16 cells and does not use the dialogue font atlas.
void menu_digits(std::vector<image::Sprite>&,std::shared_ptr<const image::Raster>,std::string_view,float x,float y,
                 std::uint8_t opacity,int layer,int order);
}
