#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace wa2::image {
class Error : public std::runtime_error {using std::runtime_error::runtime_error;};
struct TgaLayout {
    std::uint32_t width{},height{},bits{},palette_entries{},data_offset{};
    bool alpha{},top_origin{};
    std::array<std::array<std::uint8_t,4>,256> palette{};
};
struct Raster {
    std::uint32_t width{},height{};
    // Straight-alpha RGBA8, top row first. No host GPU or CPU-state dependency.
    std::vector<std::uint8_t> rgba;
};
struct Region {std::uint32_t x{},y{},width{},height{};};
// The existing game's format starts palettes/pixels at byte 18, including its
// treatment of ID and colour-map-origin fields. RLE types 9/10 are rejected.
TgaLayout inspect_game_tga(std::span<const std::uint8_t> source,bool rgba_palette=true);
Raster decode_game_tga(std::span<const std::uint8_t> source);
// Glyphs can be decoded directly from a large indexed font atlas. No complete
// RGBA atlas allocation is needed to obtain a small visible glyph rectangle.
Raster decode_game_tga_region(std::span<const std::uint8_t> source,Region region);
// Original transition masks use uncompressed Windows BMPs (8/24/32 bits).
// A 32-bit mask's alpha is intentional and must not be replaced with 255.
Raster decode_game_bmp(std::span<const std::uint8_t> source);
}
