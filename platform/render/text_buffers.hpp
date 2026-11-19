#pragma once
#include <cstdint>
namespace wa2::graphics {
void enable_glyph_packet_continuation(bool enabled) noexcept;
bool glyph_packet_continuation_enabled() noexcept;
void record_glyph_continuations(std::uint64_t count) noexcept;
std::uint64_t glyph_continuations() noexcept;
void enable_wide_glyph_packets(bool enabled) noexcept;
bool wide_glyph_packets_enabled() noexcept;
void record_wide_glyph_packet_submissions(std::uint64_t count) noexcept;
std::uint64_t wide_glyph_packet_submissions() noexcept;
void record_glyph_pipeline_rollover() noexcept;
std::uint64_t glyph_pipeline_rollovers() noexcept;
struct SurfaceGlyphCommandCounts {
    std::uint64_t submitted{},continued{},continued_tiles{};
};
void enable_surface_glyph_commands(bool enabled) noexcept;
bool surface_glyph_commands_enabled() noexcept;
void record_surface_glyph_commands(SurfaceGlyphCommandCounts frame) noexcept;
SurfaceGlyphCommandCounts surface_glyph_command_counts() noexcept;
}
