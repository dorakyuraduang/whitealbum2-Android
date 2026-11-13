#pragma once
#include <cstdint>
namespace wa2::graphics {
void enable_text_buffer_reuse(bool)noexcept;
bool text_buffer_reuse_enabled()noexcept;
void record_text_buffer_reuse()noexcept;
std::uint64_t text_buffer_reuses()noexcept;
}
