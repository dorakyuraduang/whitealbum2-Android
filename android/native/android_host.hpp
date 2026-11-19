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
enum class TextProbeMode { shipped, reference, run, submission, both };
enum class TextProbeCounter { layout_hits, command_hits, run_hits, surface_commands, surface_continued, surface_tiles, rollovers, count };
void set_performance_switch(void(*)(TextProbeMode), std::uint64_t(*)(TextProbeCounter)=nullptr) noexcept;

// Supplied by the actual ARM64 game core. There is deliberately no stub which
// could turn a launcher-only APK into an apparently successful game build.
int run_game(int argc,char** argv);
}
