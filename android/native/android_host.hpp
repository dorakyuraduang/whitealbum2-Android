#pragma once
#include <cstdint>
namespace wa2::android {
// The Android renderer must call this after a successful presentation, never
// from Choreographer or an independent display-refresh callback.
void frame_presented() noexcept;
struct GlyphTotals { std::uint64_t glyphs, tiles; };
void frame_presented_with_text(GlyphTotals (*read_totals)()) noexcept;
// Optional shared-core diagnostic switch. The independent native target does
// not register an AOT callback or link any recompiled code.
void set_performance_switch(void(*)(bool layout, bool commands),
                            std::uint64_t(*layout_hits)()=nullptr,
                            std::uint64_t(*glyph_command_hits)()=nullptr) noexcept;

// Supplied by the actual ARM64 game core. There is deliberately no stub which
// could turn a launcher-only APK into an apparently successful game build.
int run_game(int argc,char** argv);
}
