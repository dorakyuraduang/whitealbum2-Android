#pragma once
#include <cstdint>

namespace wa2::aot::fp {
using Wide=__uint128_t;
constexpr std::uint16_t Invalid=1,Denormal=2,ZeroDivide=4,Overflow=8,Underflow=16,Inexact=32,RoundUp=0x200;
struct Extended {
    std::uint64_t significand{};
    std::uint16_t sign_exponent{};
    bool sign()const noexcept{return sign_exponent>>15;}
    unsigned exponent()const noexcept{return sign_exponent&0x7fff;}
    bool zero()const noexcept{return !exponent() && !significand;}
    bool infinity()const noexcept{return exponent()==0x7fff && significand==0x8000000000000000ull;}
    bool nan()const noexcept{return exponent()==0x7fff && (significand&0x7fffffffffffffffull);}
    bool signaling()const noexcept{return nan() && !(significand&0x4000000000000000ull);}
    bool unsupported()const noexcept{return exponent() && !(significand>>63);}
    bool operator==(const Extended&)const=default;
};
struct Environment {
    std::uint16_t control{0x37f},status{};
    int minimum_exponent{-16382},maximum_exponent{16383};
    unsigned rounding()const noexcept{return (control>>10)&3;}
    unsigned precision()const noexcept{const auto p=(control>>8)&3;return p==0?24:p==2?53:64;}
    void raise(unsigned bits)noexcept{status=std::uint16_t(status|bits);}
};
Extended indefinite()noexcept;
Extended from_integer(std::int64_t)noexcept;
Extended from_scaled(Wide magnitude,int power,bool negative,Environment&);
Extended from_binary(std::uint64_t bits,unsigned width,Environment&);
std::uint64_t to_binary(Extended,unsigned width,Environment&);
std::uint64_t to_integer(Extended,unsigned width,Environment&,bool truncate=false);
Extended add(Extended,Extended,Environment&);
Extended subtract(Extended,Extended,Environment&);
Extended multiply(Extended,Extended,Environment&);
Extended divide(Extended,Extended,Environment&);
Extended square_root(Extended,Environment&);
Extended round_integral(Extended,Environment&);
Extended scale(Extended,Extended,Environment&);
// -1, 0, +1, or 2 (unordered). Ordered comparisons signal every NaN.
int compare(Extended,Extended,Environment&,bool ordered);
Extended remainder(Extended,Extended,Environment&,bool nearest);
long double to_long_double(Extended)noexcept;
Extended from_long_double(long double,Environment&);
}
