#include "clock.hpp"
namespace wa2::platform {
std::uint64_t Clock::counter() noexcept {
    return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
std::uint32_t Clock::milliseconds_since(std::chrono::steady_clock::time_point start) noexcept {
    return std::uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-start).count());
}
std::uint64_t Clock::file_time() noexcept {
    using Units=std::chrono::duration<std::int64_t,std::ratio<1,10000000>>;
    return file_time_from_unix_100ns(std::chrono::duration_cast<Units>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
}
