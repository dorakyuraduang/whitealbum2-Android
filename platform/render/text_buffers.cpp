#include "text_buffers.hpp"
#include <atomic>
namespace wa2::graphics {namespace {std::atomic<bool> enabled{false};std::atomic<std::uint64_t> reuses{};thread_local TextRenderProbe probe;}
void update_text_render_probe(TextRenderProbe value)noexcept{probe=value;}
TextRenderProbe text_render_probe()noexcept{return probe;}
void enable_text_buffer_reuse(bool value)noexcept{enabled.store(value,std::memory_order_relaxed);}
bool text_buffer_reuse_enabled()noexcept{return enabled.load(std::memory_order_relaxed);}
void record_text_buffer_reuse()noexcept{reuses.fetch_add(1,std::memory_order_relaxed);}
std::uint64_t text_buffer_reuses()noexcept{return reuses.load(std::memory_order_relaxed);}
}
