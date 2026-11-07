#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>

namespace wa2::audio {
// Mean absolute signed-16 sample values, before volume scaling. Original menu
// meters inspect at most 1024 source PCM bytes, independently of device gain.
using Levels=std::array<std::uint32_t,2>;
inline Levels scaled_levels(Levels levels,std::int32_t volume,std::int32_t master,std::int32_t category){
    if(volume<0 || volume>255 || master<0 || master>256 || category<0 || category>256 || levels[0]>32768 || levels[1]>32768)
        throw std::invalid_argument("Native audio meter gain/sample is out of range");
    // Cached 40fd98/40f9af truncate after the voice multiplier, then again
    // after the master/category product. This differs from the device's gain.
    for(auto& level:levels)level=std::uint32_t(((std::uint64_t(level)*unsigned(volume)>>8)*unsigned(master)*unsigned(category))>>16);
    return levels;
}
}
