#pragma once
#include <cstdint>

namespace wa2::text {
// Codes use the game's existing 16-bit character convention. An unmapped code
// returns the atlas's final fallback slot, preserving the original lookup.
std::uint32_t glyph_index(std::uint16_t code,bool extended=true) noexcept;
std::uint32_t glyph_count(bool extended=true) noexcept;
struct GlyphBox {std::uint32_t x{},y{},width{},height{};};
GlyphBox glyph_box(std::uint16_t code,bool extended,std::uint32_t cell,
                   std::uint32_t inset,std::uint32_t extent);
}
