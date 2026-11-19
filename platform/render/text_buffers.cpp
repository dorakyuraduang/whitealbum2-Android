#include "text_buffers.hpp"
#include <atomic>
namespace wa2::graphics {
namespace {
std::atomic<bool> enabled{true};
std::atomic<std::uint64_t> continuations{};
std::atomic<bool> wide_packets{false};
std::atomic<std::uint64_t> wide_submissions{};
std::atomic<std::uint64_t> rollovers{};
std::atomic<bool> reuse_rollover_binds{true};
std::atomic<std::uint64_t> rollover_gl_calls_avoided{};
std::atomic<bool> surface_commands{true};
std::atomic<std::uint64_t> surface_submitted{},surface_continued{},surface_tiles{};
std::atomic<std::uint64_t> geometry_hits{},geometry_draws{};
}

void enable_glyph_packet_continuation(bool value) noexcept {
    enabled.store(value, std::memory_order_relaxed);
}
bool glyph_packet_continuation_enabled() noexcept {
    return enabled.load(std::memory_order_relaxed);
}
void record_glyph_continuations(std::uint64_t count) noexcept {
    if (count) continuations.fetch_add(count, std::memory_order_relaxed);
}
std::uint64_t glyph_continuations() noexcept {
    return continuations.load(std::memory_order_relaxed);
}
void enable_wide_glyph_packets(bool value) noexcept {
    wide_packets.store(value, std::memory_order_relaxed);
}
bool wide_glyph_packets_enabled() noexcept {
    return wide_packets.load(std::memory_order_relaxed);
}
void record_wide_glyph_packet_submissions(std::uint64_t count) noexcept {
    if(count)wide_submissions.fetch_add(count, std::memory_order_relaxed);
}
std::uint64_t wide_glyph_packet_submissions() noexcept {
    return wide_submissions.load(std::memory_order_relaxed);
}
void record_glyph_pipeline_rollover() noexcept {
    rollovers.fetch_add(1,std::memory_order_relaxed);
}
std::uint64_t glyph_pipeline_rollovers() noexcept {
    return rollovers.load(std::memory_order_relaxed);
}
void enable_glyph_rollover_bind_reuse(bool value) noexcept {
    reuse_rollover_binds.store(value,std::memory_order_relaxed);
}
bool glyph_rollover_bind_reuse_enabled() noexcept {
    return reuse_rollover_binds.load(std::memory_order_relaxed);
}
std::uint64_t glyph_rollover_bindings_avoided() noexcept {
    return rollover_gl_calls_avoided.load(std::memory_order_relaxed);
}
void enable_surface_glyph_commands(bool value) noexcept {
    surface_commands.store(value,std::memory_order_relaxed);
}
bool surface_glyph_commands_enabled() noexcept {
    return surface_commands.load(std::memory_order_relaxed);
}
void record_surface_glyph_commands(SurfaceGlyphCommandCounts frame) noexcept {
    if(frame.rollover_gl_calls_avoided)rollover_gl_calls_avoided.fetch_add(frame.rollover_gl_calls_avoided,std::memory_order_relaxed);
    if(!frame.submitted && !frame.retained_geometry_draws)return;
    surface_submitted.fetch_add(frame.submitted,std::memory_order_relaxed);
    surface_continued.fetch_add(frame.continued,std::memory_order_relaxed);
    surface_tiles.fetch_add(frame.continued_tiles,std::memory_order_relaxed);
    if(frame.prepared_geometry_hits)geometry_hits.fetch_add(frame.prepared_geometry_hits,std::memory_order_relaxed);
    if(frame.retained_geometry_draws)geometry_draws.fetch_add(frame.retained_geometry_draws,std::memory_order_relaxed);
}
SurfaceGlyphCommandCounts surface_glyph_command_counts() noexcept {
    return {surface_submitted.load(std::memory_order_relaxed),surface_continued.load(std::memory_order_relaxed),
            surface_tiles.load(std::memory_order_relaxed),geometry_hits.load(std::memory_order_relaxed),
            geometry_draws.load(std::memory_order_relaxed),glyph_rollover_bindings_avoided()};
}
}
