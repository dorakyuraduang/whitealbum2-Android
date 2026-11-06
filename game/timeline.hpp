#pragma once
#include <cstdint>

namespace wa2::game {
// One shared gameplay timeline. Adapters provide nonnegative monotonic
// microseconds; time spent suspended never advances gameplay timers.
class Timeline {
    std::int64_t observed_{},logical_{};bool initialized_{},paused_{};
public:
    std::int64_t sample(std::int64_t monotonic_us);
    bool pause(bool value,std::int64_t monotonic_us);
    bool paused()const noexcept{return paused_;}
    std::int64_t microseconds()const noexcept{return logical_;}
    std::uint32_t milliseconds()const noexcept{return std::uint32_t(std::uint64_t(logical_)/1000);}
};
}
