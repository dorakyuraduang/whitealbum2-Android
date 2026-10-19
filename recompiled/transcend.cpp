#include "x87_kernel.hpp"
#include <cfenv>
#include <numbers>

namespace wa2::aot::fp::detail {
namespace {
struct Reduced {long double value;unsigned quadrant;};
Reduced reduce(Extended value){
    const int exponent=int(value.exponent())-16383;
    if(exponent < -2)return {to_long_double(value),0};
    // The original x87 trigonometric instructions reduce with a 66-bit pi.
    // Keeping that value is observable near multiples of pi, even after a
    // float32 store. All arithmetic here is identical on x64 and ARM64.
    constexpr Wide Pi=(Wide(0xc90fdaa22168c234ull)<<2)|3;
    const Wide numerator=Wide(value.significand)<<unsigned(exponent+2);
    auto quotient=numerator/Pi,remainder=numerator%Pi;
    bool negative=false;
    if(remainder>Pi-remainder || (remainder==Pi-remainder && (quotient&1))){++quotient;remainder=Pi-remainder;negative=true;}
    if(value.sign())negative=!negative;
    Environment temporary;const auto reduced=from_scaled(remainder,-65,negative,temporary);
    auto quadrant=unsigned(quotient)&3;if(value.sign())quadrant=(4-quadrant)&3;
    return {to_long_double(reduced),quadrant};
}
}
Extended transcend(unsigned operation,Extended first,Extended second,Environment& environment){
    if(first.unsupported() || first.nan())return add(first,first,environment);
    if((operation==0xf1 || operation==0xf3 || operation==0xf9) && (second.unsupported() || second.nan()))return add(second,second,environment);
    if((operation==0xfe || operation==0xff || operation==0xf2) && first.infinity()){
        environment.raise(Invalid);return indefinite();
    }
    if(operation==0xf1 && first.zero()){
        if(second.zero()){environment.raise(Invalid);return indefinite();}
        environment.raise(ZeroDivide);return {0x8000000000000000ull,std::uint16_t(second.sign()?0x7fff:0xffff)};
    }
    if(operation==0xf1 && first.sign()){
        environment.raise(Invalid);return indefinite();
    }
    if(operation==0xf1 && (first.infinity() || second.infinity())){
        const auto relation=compare(first,from_integer(1),environment,false);
        if(second.zero() || relation==0){environment.raise(Invalid);return indefinite();}
        return {0x8000000000000000ull,std::uint16_t((second.sign()!=(relation<0))?0xffff:0x7fff)};
    }
    if(operation==0xf0 && !first.infinity() && int(first.exponent())-16383>=0 &&
       (first.exponent()!=16383 || first.significand!=0x8000000000000000ull)){
        environment.raise(Inexact);return first;
    }
    std::fenv_t previous;std::feholdexcept(&previous);
    const auto x=to_long_double(first),y=to_long_double(second);long double result;
    switch(operation){
    case 0xfe:{const auto r=reduce(first);result=r.quadrant&1?std::cos(r.value):std::sin(r.value);if(r.quadrant&2)result=-result;break;}
    case 0xff:{const auto r=reduce(first);result=r.quadrant&1?std::sin(r.value):std::cos(r.value);if(r.quadrant==1 || r.quadrant==2)result=-result;break;}
    case 0xf2:{const auto r=reduce(first);result=std::tan(r.value);if(r.quadrant&1)result=-1/result;break;}
    case 0xf1:result=y*std::log2(x);break;
    case 0xf9:result=y*std::log1p(x)*std::numbers::log2e_v<long double>;break;
    case 0xf3:result=std::atan2(y,x);break;
    case 0xf0:result=std::expm1(x*std::numbers::ln2_v<long double>);break;
    default:std::fesetenv(&previous);throw std::logic_error("unknown statically selected transcendental function");
    }
    const auto raised=std::fetestexcept(FE_ALL_EXCEPT);std::fesetenv(&previous);
    environment.status&=~RoundUp;
    environment.raise((raised&FE_INVALID?Invalid:0)|(raised&FE_DIVBYZERO?ZeroDivide:0)|
                      (raised&FE_OVERFLOW?Overflow:0)|(raised&FE_UNDERFLOW?Underflow:0)|(raised&FE_INEXACT?Inexact:0));
    if(operation==0xf1 && !second.zero() && first!=from_integer(1))environment.raise(Inexact);
    return from_long_double(result,environment);
}
}
