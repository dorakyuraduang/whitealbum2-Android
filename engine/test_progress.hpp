#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace wa2::testing {
// Existing static functions 0x405250/0x4052d0/0x405390 load these
// three unencoded Sys.sav blocks; 0x405370 writes the final block.
inline constexpr std::size_t read_history_bytes=0x80000;
inline constexpr std::size_t gallery_bytes=2000000;
inline constexpr std::size_t system_flags_offset=read_history_bytes+gallery_bytes;
inline constexpr std::size_t system_flags_bytes=0x1000;
inline constexpr std::size_t system_save_bytes=system_flags_offset+system_flags_bytes;
// 0x4259f0 checks 0/3/5 for the main chapter choices and bit zero of
// 20..25/30..33 for the completed-ending menu. No unknown flags are set.
inline constexpr std::array<unsigned,14> chapter_flags={0,3,4,5,20,21,22,23,24,25,30,31,32,33};
inline bool unlock_chapters(std::span<std::uint8_t> flags){
    if(flags.size()!=system_flags_bytes)throw std::invalid_argument("Unsupported test system flag block");
    bool changed=false;
    for(const auto flag:chapter_flags){auto& low=flags[flag*4];changed|=!(low&1);low|=1;}
    return changed;
}
}
