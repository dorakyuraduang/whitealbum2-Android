#pragma once
#include <atomic>
#include <cstdint>
namespace wa2::aot {
inline constexpr bool default_text_page_replay=true;
inline std::atomic<bool> text_page_replay_enabled{default_text_page_replay};
inline thread_local std::uint64_t text_page_replay_spans{},text_page_replay_glyphs{};
}
