#pragma once
#include <cstdint>
namespace wr {struct Cpu;class Memory;}
namespace wa2::aot {
struct NativeGlyphCounts {std::uint64_t accepted{},declined{},tiles{};};
NativeGlyphCounts native_glyph_counts();
bool native_draw_glyph(wr::Cpu&,wr::Memory&);
}
