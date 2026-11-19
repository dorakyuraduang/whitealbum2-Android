#pragma once
#include <cstdint>
namespace wa2::android {
// The Android renderer must call this after a successful presentation, never
// from Choreographer or an independent display-refresh callback.
void frame_presented() noexcept;
struct GlyphTotals {
    std::uint64_t glyphs, tiles, replayed_glyphs, reused_geometry, rollovers, avoided_driver_calls;
    std::uint64_t particle_commands, particle_tiles, particle_continuations;
    std::uint64_t small_weather_updates, small_weather_particles, small_weather_batched_updates;
};
void frame_presented_with_text(GlyphTotals (*read_totals)()) noexcept;
// Optional shared-core diagnostic switch. The independent native target does
// not register an AOT callback or link any recompiled code.
enum class TextProbeMode { shipped, reference, run, submission, both, page_reference, page, dense, bindings, novel,
                           particle_reference, particles, weather_reference, weather };
enum class TextProbeCounter { layout_hits, command_hits, run_hits, surface_commands, surface_continued, surface_tiles, rollovers, page_spans, page_glyphs, prepared_geometry, retained_draws, binding_calls_avoided, count };
void set_performance_switch(void(*)(TextProbeMode), std::uint64_t(*)(TextProbeCounter)=nullptr) noexcept;

// Supplied by the actual ARM64 game core. There is deliberately no stub which
// could turn a launcher-only APK into an apparently successful game build.
int run_game(int argc,char** argv);
}
