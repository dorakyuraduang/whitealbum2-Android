#pragma once
#include <chrono>
#include <cstdint>
#include <map>
namespace wa2::platform {
struct Clock {
    static constexpr std::uint64_t frequency=1000000000;
    static std::uint64_t counter() noexcept;
    static std::uint32_t milliseconds_since(std::chrono::steady_clock::time_point start) noexcept;
    static std::uint64_t file_time() noexcept;
    static constexpr std::uint64_t file_time_from_unix_100ns(std::int64_t value) noexcept {
        return std::uint64_t(value)+116444736000000000ull;
    }
};
struct TimerCaps {std::uint32_t minimum{},maximum{};bool valid{};};
// An OS backend owns timer policy; logical game waits use monotonic time.
class TimerResolution {
    std::map<std::uint32_t,unsigned> periods_;
public:
    TimerResolution()=default;
    TimerResolution(const TimerResolution&)=delete;
    TimerResolution& operator=(const TimerResolution&)=delete;
    ~TimerResolution();
    static TimerCaps caps() noexcept;
    bool begin(std::uint32_t period);
    bool end(std::uint32_t period) noexcept;
};
}
