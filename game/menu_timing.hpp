#pragma once
#include <cstdint>
#include <stdexcept>

namespace wa2::game {
// Source menu time units are 60 Hz logic ticks, independently of presentation.
struct MenuTiming {
    std::uint32_t open{15},page{7},close{15};
    void validate()const{if(open>600 || page>600 || close>600)throw std::invalid_argument("Native menu timing exceeds its bound");}
};
}
