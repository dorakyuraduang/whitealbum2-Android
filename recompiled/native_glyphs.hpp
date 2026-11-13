#pragma once
#include <cstdint>
namespace wr {struct Cpu;class Memory;}
namespace wa2::aot {
struct NativeGlyphCounts {std::uint64_t accepted{},declined{},tiles{};};
NativeGlyphCounts native_glyph_counts();
bool native_draw_glyph(wr::Cpu&,wr::Memory&);
// A generated entry may return without consuming its source call on budget
// exhaustion. The next Program entry replenishes the allowance and resumes
// the same glyph. Unsupported geometry still takes the normal static body.
bool native_draw_glyph_or_yield(wr::Cpu&,wr::Memory&);
}
