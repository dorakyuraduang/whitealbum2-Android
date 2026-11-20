#pragma once
#include <cstdint>

namespace wa2::aot {
// Source ABI/data bindings, independent of OS, compiler and presentation.
// Callers must prove their immutable source function before invoking a port.
struct SpriteSource {
    std::uint32_t sprite_entry,glyph_entry,globals,images,palette,half_pixel;
    std::uint32_t sse2,radians,cosine_words,sine_words,font_ranges,font_styles;
    std::uint32_t first_weather{},trig_dispatch{};
};
inline constexpr SpriteSource main_sprite_source{
    0x43b7a0,0x4462e0,0xb34000,0xa93fb8,0x4c2290,0x4c2280,
    0xbc830c,0x4a22f0,0x4a84d0,0x4ac710,0x4a14b8,0xa823b3,
    0xab3fb8+81*256,0
};
}
