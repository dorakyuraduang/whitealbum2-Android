#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <optional>

namespace wa2::geometry {
#if defined(WA2_AOT_DIAGNOSTICS) && WA2_AOT_DIAGNOSTICS
inline thread_local bool single_tile_reference_enabled=false;
struct SingleTileDiagnostics {std::uint64_t attempts{},accepted{},integer_glyphs{};};
inline thread_local SingleTileDiagnostics single_tile_diagnostics;
#endif
struct SingleTileLayout {unsigned index;std::array<std::array<float,2>,4> uv;};
// Exact conversions, with no floating-point instruction or rounding. Reject
// values that cannot be represented in 1/256 units; never quantize an input.
inline std::optional<std::int32_t> fixed_coordinate(std::uint32_t bits){
    const auto magnitude=bits&0x7fffffffu;
    if(bits==0x80000000u || magnitude>0x47000000u)return {};
    if(!magnitude)return 0;
    const int shift=142-int((bits>>23)&255);
    if(shift<0 || shift>=24)return {};
    const auto significand=(bits&0x7fffffu)|0x800000u;
    if(significand&((1u<<shift)-1))return {};
    const auto value=std::int32_t(significand>>shift);
    return bits>>31?-value:value;
}
inline std::uint32_t dyadic_bits(std::int32_t value,unsigned denominator_bits){
    // Callers validate |value| <= 2^23 and denominator_bits <= 23.
    if(!value)return 0;
    const auto magnitude=std::uint32_t(value<0?-value:value),top=31u-std::countl_zero(magnitude);
    return (value<0?0x80000000u:0u)|((127u+top-denominator_bits)<<23)|((magnitude<<(23-top))&0x7fffffu);
}
inline bool dyadic_coordinate(float value){
    const auto bits=std::bit_cast<std::uint32_t>(value),magnitude=bits&0x7fffffffu;
    if(bits==0x80000000u || magnitude>0x47000000u || (magnitude && magnitude<0x00800000u))return false;
    const auto scaled=value*256.f;return float(int(scaled))==scaled;
}
// In one power-of-two cell, dyadic UVs produce exact grid endpoints 0 and 1.
// Bounded dyadic corners make every (b-a)*t+a endpoint exact in PC24. Thus the
// original interpolation returns the corners themselves, including their bits.
inline std::optional<SingleTileLayout> single_tile_geometry(const std::array<std::array<float,2>,4>& corners,
    const std::array<float,4>& crop,unsigned tw,unsigned th,unsigned nx,unsigned ny){
#if defined(WA2_AOT_DIAGNOSTICS) && WA2_AOT_DIAGNOSTICS
    if(single_tile_reference_enabled)return {};
    ++single_tile_diagnostics.attempts;
#endif
    if(!tw || !th || tw>32768 || th>32768 || (tw&(tw-1)) || (th&(th-1)) ||
       !nx || !ny || nx>16 || ny>16)return {};
    for(const auto& p:corners)for(const auto v:p)if(!dyadic_coordinate(v))return {};
    for(const auto v:crop)if(!dyadic_coordinate(v))return {};
    const auto [u0,u1,v0,v1]=crop;
    if(u0<0 || v0<0 || u1<=u0 || v1<=v0)return {};
    const auto x=unsigned(u0)/tw,y=unsigned(v0)/th;
    if(x>=nx || y>=ny || u1>float((x+1)*tw) || v1>float((y+1)*th))return {};
    const float left=(u0-float(x*tw))/float(tw),right=(u1-float(x*tw))/float(tw),
                top=(v0-float(y*th))/float(th),bottom=(v1-float(y*th))/float(th);
#if defined(WA2_AOT_DIAGNOSTICS) && WA2_AOT_DIAGNOSTICS
    ++single_tile_diagnostics.accepted;
#endif
    return SingleTileLayout{y*nx+x,{{{left,top},{right,top},{left,bottom},{right,bottom}}}};
}
}
