#pragma once
#include <atomic>
#include <cstdint>

namespace wa2::aot {
// Both policies run the same checked weather body. The reference retains
// the original cutoff for same-scene phone comparisons.
inline constexpr unsigned small_weather_minimum=1, original_weather_minimum=64;
inline std::atomic<bool> small_weather_batches{true};
struct WeatherThresholdCounts {std::uint64_t updates{},batched_updates{},particles{};};
inline thread_local WeatherThresholdCounts small_weather_counts{};
}
