#pragma once
#include <cstdint>

namespace wa2 {
// Shared by game systems, matching the existing CRT's seed/state progression.
class Random {
    std::uint32_t state_{1};
public:
    explicit Random(std::uint32_t seed=1):state_(seed){}
    void seed(std::uint32_t value)noexcept{state_=value;}
    std::uint32_t next()noexcept{state_=state_*214013u+2531011u;return (state_>>16)&32767u;}
};
}
