#pragma once
#include <atomic>
#include <cstdint>

namespace wa2::aot {
inline std::atomic<bool> text_layout_replay_enabled{true};
inline thread_local std::uint64_t text_layout_replay_hits{};
inline thread_local std::uint64_t text_layout_replay_captures{};
}
