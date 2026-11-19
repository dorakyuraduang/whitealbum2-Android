#pragma once
#include <atomic>
#include <cstdint>

namespace wa2::aot {
inline constexpr bool default_text_run_replay=false;
inline std::atomic<bool> text_run_replay_enabled{default_text_run_replay};
inline thread_local std::uint64_t text_run_replay_hits{};
inline thread_local std::uint64_t text_run_replay_captures{};
}
