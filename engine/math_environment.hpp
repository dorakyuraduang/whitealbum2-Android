#pragma once
#include "scalar_math.hpp"
#include <bit>
#include <cerrno>
#include <cfenv>
#include <stdexcept>
namespace wa2::math::detail {
struct NativeEnvironment {
    std::fenv_t saved;int error=errno;
    NativeEnvironment(){
        if(std::feholdexcept(&saved))throw std::runtime_error("Cannot preserve native math environment");
        if(std::fesetenv(FE_DFL_ENV) || std::fesetround(FE_TONEAREST)){std::fesetenv(&saved);errno=error;throw std::runtime_error("Cannot set native IEEE math environment");}
    }
    ~NativeEnvironment(){std::fesetenv(&saved);errno=error;}
    ScalarResult finish(double value)const {
        const auto raised=std::fetestexcept(FE_ALL_EXCEPT);
        return {value,std::uint16_t((raised&FE_INVALID?1:0)|(raised&FE_DIVBYZERO?4:0)|(raised&FE_OVERFLOW?8:0)|
            (raised&FE_UNDERFLOW?16:0)|(raised&FE_INEXACT?32:0))};
    }
};
inline bool finite(double value){return ((std::bit_cast<std::uint64_t>(value)>>52)&0x7ff)!=0x7ff;}
}
