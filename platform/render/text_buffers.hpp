#pragma once
#include <cstdint>
namespace wa2::graphics {
struct TextRenderProbe {
    std::uint64_t presents{},draws{},submitted{},packets{},glyph_packets{},state_applies{},uploads{},
        function_entries{},api_calls{},live_objects{},retained_packets{},retained_bytes{},fp_control{},monotonic_ns{};
    std::uint64_t draw_ns{},apply_ns{},flush_ns{},upload_ns{},swap_ns{},function_ns{},api_ns{},sleep_ns{};
};
// Owner-thread counters are copied only at an existing successful present.
void update_text_render_probe(TextRenderProbe)noexcept;
TextRenderProbe text_render_probe()noexcept;
void enable_text_buffer_reuse(bool)noexcept;
bool text_buffer_reuse_enabled()noexcept;
void record_text_buffer_reuse()noexcept;
std::uint64_t text_buffer_reuses()noexcept;
}
