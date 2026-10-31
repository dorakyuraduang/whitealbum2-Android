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
// Named completion/second-playthrough records in the resource flag catalogue.
inline constexpr std::array<unsigned,4> extra_completion_flags={1,2,10,11};
// The music browser at 0x428400 reads 100 + the track IDs at 0x4c1dd0.
// Flag 135 selects the alternate version of track 21; 134 is the bonus track.
inline constexpr std::array<unsigned,64> music_flags={
    101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,
    117,118,119,120,121,122,123,124,126,127,128,129,131,132,133,134,
    135,140,141,142,143,144,145,146,147,150,151,152,153,154,155,156,
    157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,174,
};
inline constexpr std::array<unsigned,16> movie_flags={
    200,201,202,203,207,208,209,210,211,212,213,220,221,222,223,224
};
// The 24 replay records at 0x4c1bf0 use these system flag indices.
inline constexpr std::array<unsigned,24> replay_flags={
    80,300,301,302,303,304,305,306,307,308,309,310,
    311,312,313,314,315,316,317,318,319,320,321,322
};
inline constexpr auto content_flags=[] {
    std::array<unsigned,122> result{};std::size_t at=0;
    const auto append=[&](const auto& flags){for(auto flag:flags)result[at++]=flag;};
    append(chapter_flags);append(extra_completion_flags);append(music_flags);
    append(movie_flags);append(replay_flags);return result;
}();
// Nonzero variant IDs in the 168 x 10 gallery table at 0x4c01b0.
// Index zero is a placeholder, NOT a CG. Marking the entire gallery block
// would make the static gallery enumerator expose empty/nonexistent variants.
inline constexpr std::size_t gallery_bank_bytes=1000000;
inline constexpr std::array<unsigned,272> gallery_variants={
    100100,100200,100300,100301,100400,100500,100600,100700,100800,100801,
    100802,101000,101100,101200,101201,101202,101203,101204,101205,101206,
    101207,101300,101400,101500,101600,101700,101701,101800,101900,101901,
    102000,102001,102200,102201,102202,102203,102204,102300,102400,102500,
    102600,102700,103200,103300,200000,200001,200002,200100,200101,200102,
    200103,200104,200200,200201,200300,200302,200400,200500,200501,200600,
    200700,200701,200800,200801,200802,200803,200804,200900,200901,201000,
    201001,201100,201101,201102,201103,201104,201200,201300,201301,201400,
    201500,201501,201600,201601,201700,201701,201800,201801,201900,202000,
    202100,202200,202201,202202,202300,202301,202400,202401,202402,202500,
    202600,202700,202701,202702,202703,202704,202800,202801,202802,202900,
    202901,203000,203100,203200,203300,203400,203401,203402,203403,203404,
    203405,203406,203500,203600,203700,203701,203800,203900,203901,204000,
    204001,204100,204101,204200,204300,204400,204500,204600,204601,204700,
    204800,204900,205000,205001,205002,205003,205100,205101,205102,205200,
    205201,205300,205301,205302,205400,205401,205402,205500,205501,205600,
    205601,205700,205800,205801,205900,205901,205902,205903,205904,206000,
    206100,206101,206102,206200,206300,206400,206500,206600,206601,206700,
    206800,206801,206802,206900,206901,207000,207001,207002,207003,207004,
    207100,207200,207300,207400,207600,208000,208100,208101,208102,208200,
    208300,208400,208500,208600,208601,208602,208603,208700,208701,208702,
    208800,208900,209000,209100,209101,209200,209300,209400,209500,209700,
    209701,209702,209703,209800,209900,209901,209902,210000,210001,210002,
    210100,210200,210300,210301,210400,210401,210402,210403,210404,210405,
    210406,210407,210500,210501,210502,210503,210600,210601,210602,210603,
    210604,210605,210700,210701,210800,210801,210900,211000,211001,211100,
    211200,211201,211300,211400,220000,220100,220200,220201,220400,220500,
    220600,220601,
};
inline bool unlock_all_content(std::span<std::uint8_t> save){
    if(save.size()!=system_save_bytes)throw std::invalid_argument("Unsupported test system save");
    bool changed=false;
    for(auto& value:save.first(read_history_bytes)){changed|=value!=0xff;value=0xff;}
    const auto unlock=[&](std::size_t offset){auto& value=save[offset];changed|=!(value&1);value|=1;};
    for(std::size_t bank=0;bank<2;++bank)
        for(auto variant:gallery_variants)unlock(read_history_bytes+bank*gallery_bank_bytes+variant);
    // Unknown flags, choice-state flags 99/900, and every high bit survive.
    for(auto flag:content_flags)unlock(system_flags_offset+flag*4);
    return changed;
}
}
