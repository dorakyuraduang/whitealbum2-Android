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
}
