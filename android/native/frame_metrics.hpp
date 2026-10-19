#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace wa2::android {
struct FrameSnapshot {
    double fps{}, average_ms{}, p95_ms{}, maximum_ms{};
    std::uint64_t presented{}, intervals{};
    std::size_t percentile_samples{};
};

// One producer: call only after the renderer successfully submits a frame.
// Time is injected so lifecycle and stall behavior can be tested without a
// running game, timers, a display or any performance pressure test.
class FrameMetrics {
public:
    static constexpr std::size_t capacity=512;
    explicit FrameMetrics(std::int64_t update_ns=250000000) noexcept;
    void reset() noexcept;
    std::optional<FrameSnapshot> presented(std::int64_t monotonic_ns) noexcept;

private:
    std::array<std::int64_t,capacity> intervals_{};
    std::int64_t update_ns_,last_{-1},window_{-1},maximum_{};
    std::uint64_t total_{},count_{};
    std::size_t samples_{},next_{};
};
}
