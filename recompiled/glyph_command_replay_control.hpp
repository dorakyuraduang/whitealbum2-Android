#pragma once
#include <atomic>
#include <cstdint>

namespace wa2::aot {
inline std::atomic<bool> glyph_command_replay_enabled{true};
inline thread_local std::uint64_t glyph_command_replay_hits{};
inline thread_local std::uint64_t glyph_command_replay_captures{};
}
