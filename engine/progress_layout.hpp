#pragma once
#include <cstddef>

namespace wa2::progress::layout {
// The original Sys.sav file format, not a mapped machine-memory image.
inline constexpr std::size_t read_history_bytes=0x80000;
inline constexpr std::size_t gallery_bank_bytes=1000000;
inline constexpr std::size_t gallery_bytes=2*gallery_bank_bytes;
inline constexpr std::size_t system_flags_offset=read_history_bytes+gallery_bytes;
inline constexpr std::size_t system_flags_bytes=0x1000;
inline constexpr std::size_t system_save_bytes=system_flags_offset+system_flags_bytes;
inline constexpr std::size_t script_count=1024;
inline constexpr std::size_t lines_per_script=4096;
inline constexpr std::size_t flag_count=system_flags_bytes/4;
}
