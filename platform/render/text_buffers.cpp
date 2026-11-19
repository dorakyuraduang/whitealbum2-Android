#include "text_buffers.hpp"
#include <atomic>
namespace wa2::graphics {
namespace {
std::atomic<bool> enabled{true};
std::atomic<std::uint64_t> continuations{};
std::atomic<bool> wide_packets{true};
std::atomic<std::uint64_t> wide_submissions{};
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
}
