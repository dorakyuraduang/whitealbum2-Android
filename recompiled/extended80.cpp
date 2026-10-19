#include "extended80.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace wa2::aot::fp {
namespace {
constexpr std::uint64_t IntegerBit=0x8000000000000000ull,QuietBit=0x4000000000000000ull;
constexpr Wide One=1;
Wide jam(Wide value,unsigned amount)noexcept{
    if(!amount)return value;
    if(amount>=128)return value?1:0;
    return (value>>amount)|bool(value&((One<<amount)-1));
}
unsigned width(Wide value)noexcept{
    const auto high=std::uint64_t(value>>64);
    return high?128-unsigned(std::countl_zero(high)):value?64-unsigned(std::countl_zero(std::uint64_t(value))):0;
}
Extended infinity(bool sign)noexcept{return {IntegerBit,std::uint16_t((sign?0x8000:0)|0x7fff)};}
Extended quiet(Extended value)noexcept{value.significand|=IntegerBit|QuietBit;return value;}
void denormal_operands(Extended a,Extended b,Environment& environment){
    if((!a.exponent() && a.significand) || (!b.exponent() && b.significand))environment.raise(Denormal);
}
Extended special(Extended a,Extended b,Environment& environment){
    if(a.unsupported() || b.unsupported()){environment.raise(Invalid);return indefinite();}
    if(a.signaling() || b.signaling())environment.raise(Invalid);
    if(!a.nan())return quiet(b);
    if(!b.nan())return quiet(a);
    // x87 chooses a quiet NaN over a signaling one, then the greater payload.
    if(a.signaling()!=b.signaling())return quiet(a.signaling()?b:a);
    if(a.significand!=b.significand)return quiet(a.significand>b.significand?a:b);
    return quiet(a.sign()?b:a);
}
struct Normal {std::uint64_t significand;int exponent;};
Normal normalize(Extended value,Environment& environment){
    int exponent=value.exponent()?int(value.exponent())-16383:-16382;
    if(!value.exponent() && value.significand)environment.raise(Denormal);
    const auto shift=unsigned(std::countl_zero(value.significand));
    return {value.significand<<shift,exponent-int(shift)};
}
bool increment(Wide retained,Wide discarded,unsigned shift,bool sign,unsigned rounding)noexcept{
    if(!discarded)return false;
    if(rounding==1)return sign;
    if(rounding==2)return !sign;
    if(rounding==3)return false;
    const auto half=One<<(shift-1);
    return discarded>half || (discarded==half && (retained&1));
}
Extended pack(bool sign,int exponent,Wide fraction,Environment& environment,unsigned precision=0){
    if(!fraction)return {0,std::uint16_t(sign?0x8000:0)};
    const int lead=int(width(fraction))-1;
    if(lead>126){fraction=jam(fraction,unsigned(lead-126));exponent+=lead-126;}
    else if(lead<126){fraction<<=126-lead;exponent-=126-lead;}
    const bool tiny=exponent < environment.minimum_exponent;
    if(tiny){fraction=jam(fraction,unsigned(std::min(32768,environment.minimum_exponent-exponent)));exponent=environment.minimum_exponent;}
    const auto p=precision?precision:environment.precision();
    const auto drop=127-p;
    auto retained=fraction>>drop;
    const auto discarded=fraction&((One<<drop)-1);
    const bool up=increment(retained,discarded,drop,sign,environment.rounding());
    if(discarded)environment.raise(Inexact|(tiny?Underflow:0));
    environment.status&=~RoundUp;
    if(up){++retained;environment.raise(RoundUp);}
    if(retained>=(One<<p)){retained>>=1;++exponent;}
    if(exponent>environment.maximum_exponent){
        environment.raise(Overflow|Inexact);
        const bool inf=environment.rounding()==0 || (environment.rounding()==1 && sign) || (environment.rounding()==2 && !sign);
        if(inf)environment.raise(RoundUp);else environment.status&=~RoundUp;
        if(inf)return infinity(sign);
        return {std::uint64_t((One<<p)-1)<<(64-p),std::uint16_t((sign?0x8000:0)|(environment.maximum_exponent+16383))};
    }
    auto significand=std::uint64_t(retained)<<(64-p);
    if(environment.minimum_exponent!=-16382 && significand && !(significand&IntegerBit)){
        const auto shift=unsigned(std::countl_zero(significand));significand<<=shift;exponent-=int(shift);
    }
    const unsigned biased=significand&IntegerBit?unsigned(exponent+16383):0;
    return {significand,std::uint16_t((sign?0x8000:0)|biased)};
}
void condition(Environment& environment,unsigned quotient){
    environment.status&=~(0x100|0x200|0x400|0x4000);
    environment.status|=std::uint16_t((quotient&4?0x100:0)|(quotient&2?0x4000:0)|(quotient&1?0x200:0));
}
bool normal24(Extended value){
    return value.exponent() && value.exponent()!=0x7fff && (value.significand&IntegerBit) && !(value.significand&0xffffffffffull);
}
bool round24(bool sign,int power,std::uint64_t magnitude,Environment& environment,Extended& result){
    if(!magnitude)return false;
    const auto lead=63-unsigned(std::countl_zero(magnitude));auto exponent=power+int(lead);
    // Boundary exponents and subnormals retain the full extended packer.
    if(exponent<environment.minimum_exponent || exponent>=environment.maximum_exponent || exponent<-16382 || exponent>=16383)return false;
    const auto drop=lead>23?lead-23:0;
    auto retained=drop?magnitude>>drop:magnitude<<(23-lead);
    const auto discarded=drop?magnitude&((std::uint64_t(1)<<drop)-1):0;
    const auto mode=environment.rounding();
    const bool up=discarded && (mode==1?sign:mode==2?!sign:mode==3?false:
        discarded>(std::uint64_t(1)<<(drop-1)) || (discarded==(std::uint64_t(1)<<(drop-1)) && (retained&1)));
    environment.status&=~RoundUp;if(discarded)environment.raise(Inexact);
    if(up){++retained;environment.raise(RoundUp);}
    if(retained>=(std::uint64_t(1)<<24)){retained>>=1;++exponent;}
    result={retained<<40,std::uint16_t((sign?0x8000:0)|(exponent+16383))};return true;
}
}
Extended indefinite()noexcept{return {0xc000000000000000ull,0xffff};}
Extended from_integer(std::int64_t value)noexcept{
    if(!value)return {};
    const auto negative=value<0;const auto magnitude=negative?0-std::uint64_t(value):std::uint64_t(value);
    const auto leading=unsigned(std::countl_zero(magnitude));
    return {magnitude<<leading,std::uint16_t((negative?0x8000:0)|(16383+63-leading))};
}
Extended from_scaled(Wide magnitude,int power,bool negative,Environment& environment){
    return pack(negative,power+126,magnitude,environment,64);
}
Extended from_binary(std::uint64_t bits,unsigned size,Environment& environment){
    const auto fraction_bits=size==32?23u:52u,exponent_bits=size==32?8u:11u;
    const auto exponent_mask=(1u<<exponent_bits)-1;
    const auto sign=bool(bits>>(size-1));
    const auto exponent=unsigned(bits>>fraction_bits)&exponent_mask;
    const auto fraction=bits&((std::uint64_t(1)<<fraction_bits)-1);
    environment.status&=~RoundUp;
    if(exponent==exponent_mask){
        if(!fraction)return infinity(sign);
        if(!(fraction&(std::uint64_t(1)<<(fraction_bits-1))))environment.raise(Invalid);
        return {IntegerBit|QuietBit|(fraction<<(63-fraction_bits)),std::uint16_t((sign?0x8000:0)|0x7fff)};
    }
    if(!exponent && !fraction)return {0,std::uint16_t(sign?0x8000:0)};
    const auto bias=(1<<(exponent_bits-1))-1;
    auto significand=(fraction|(exponent?std::uint64_t(1)<<fraction_bits:0))<<(63-fraction_bits);
    int unbiased=exponent?int(exponent)-bias:1-bias;
    if(!exponent){environment.raise(Denormal);const auto shift=unsigned(std::countl_zero(significand));significand<<=shift;unbiased-=int(shift);}
    return {significand,std::uint16_t((sign?0x8000:0)|(unbiased+16383))};
}
std::uint64_t to_binary(Extended value,unsigned size,Environment& environment){
    const unsigned precision=size==32?24:53,exponent_bits=size==32?8:11;
    const auto sign=std::uint64_t(value.sign())<<(size-1);
    const int bias=(1<<(exponent_bits-1))-1;
    const auto inf=std::uint64_t((1u<<exponent_bits)-1)<<(precision-1);
    environment.status&=~RoundUp;
    if(value.unsupported()){environment.raise(Invalid);return sign|inf|(std::uint64_t(1)<<(precision-2));}
    if(value.nan()){
        if(value.signaling())environment.raise(Invalid);
        return sign|inf|(std::uint64_t(1)<<(precision-2))|((value.significand&~IntegerBit)>>(64-precision));
    }
    if(value.infinity())return sign|inf;
    if(value.zero())return sign;
    const auto exact_exponent=int(value.exponent())-16383;
    const unsigned exact_drop=64-precision;
    if(value.exponent() && exact_exponent>=1-bias && exact_exponent<=bias && !(value.significand&((std::uint64_t(1)<<exact_drop)-1)))
        return sign|(std::uint64_t(exact_exponent+bias)<<(precision-1))|((value.significand>>exact_drop)&((std::uint64_t(1)<<(precision-1))-1));
    // Stores do not signal the x87 denormal-operand exception.
    auto temporary=environment;auto normalized=normalize(value,temporary);
    auto fraction=Wide(normalized.significand)<<63;
    auto exponent=normalized.exponent;
    const bool tiny=exponent<1-bias;
    if(tiny){fraction=jam(fraction,unsigned(std::min(32768,1-bias-exponent)));exponent=1-bias;}
    const unsigned drop=127-precision;
    auto retained=fraction>>drop;const auto discarded=fraction&((One<<drop)-1);
    const bool up=increment(retained,discarded,drop,value.sign(),environment.rounding());
    if(discarded)environment.raise(Inexact|(tiny?Underflow:0));
    if(up){++retained;environment.raise(RoundUp);}
    if(retained>=(One<<precision)){retained>>=1;++exponent;}
    if(exponent>bias){
        environment.raise(Overflow|Inexact);
        const bool use_inf=environment.rounding()==0 || (environment.rounding()==1 && value.sign()) || (environment.rounding()==2 && !value.sign());
        if(use_inf)environment.raise(RoundUp);else environment.status&=~RoundUp;
        return sign|(use_inf?inf:inf-1);
    }
    const auto leading=std::uint64_t(1)<<(precision-1);
    const auto biased=retained>=leading?unsigned(exponent+bias):0;
    return sign|(std::uint64_t(biased)<<(precision-1))|(std::uint64_t(retained)&(leading-1));
}
std::uint64_t to_integer(Extended value,unsigned bits,Environment& environment,bool truncate){
    environment.status&=~RoundUp;
    const auto invalid=[&](){environment.raise(Invalid);return std::uint64_t(1)<<(bits-1);};
    if(value.unsupported() || value.nan() || value.infinity())return invalid();
    if(value.zero())return 0;
    const auto small_exponent=int(value.exponent())-16383;
    if((bits==32 || bits==64) && small_exponent>=0 && small_exponent<=30){
        const auto drop=unsigned(63-small_exponent);auto retained=value.significand>>drop;
        const auto discarded=value.significand&((std::uint64_t(1)<<drop)-1);const auto mode=truncate?3:environment.rounding();
        const bool up=discarded && (mode==1?value.sign():mode==2?!value.sign():mode==3?false:
            discarded>(std::uint64_t(1)<<(drop-1)) || (discarded==(std::uint64_t(1)<<(drop-1)) && (retained&1)));
        if(up)++retained;const auto limit=std::uint64_t(1)<<(bits-1);
        if(retained>limit || (!value.sign() && retained==limit))return invalid();
        if(discarded)environment.raise(Inexact|(up?RoundUp:0));
        return value.sign()?0-retained:retained;
    }
    auto temporary=environment;const auto n=normalize(value,temporary);
    if(n.exponent>63)return invalid();
    Wide retained{},discarded{};unsigned drop{};
    if(n.exponent>=63)retained=n.significand;
    else{
        drop=unsigned(63-n.exponent);
        if(drop>=128){retained=0;discarded=1;drop=127;}
        else{retained=Wide(n.significand)>>drop;discarded=Wide(n.significand)&((One<<drop)-1);}
    }
    const bool up=discarded && increment(retained,discarded,drop,value.sign(),truncate?3:environment.rounding());
    if(up)++retained;
    const Wide limit=One<<(bits-1);
    if(retained>limit || (!value.sign() && retained==limit))return invalid();
    if(discarded)environment.raise(Inexact|(up?RoundUp:0));
    const auto magnitude=std::uint64_t(retained);
    return value.sign()?0-magnitude:magnitude;
}
Extended add(Extended a,Extended b,Environment& environment){
    environment.status&=~RoundUp;
    if(a.unsupported() || b.unsupported() || a.nan() || b.nan())return special(a,b,environment);
    if(a.infinity() || b.infinity()){
        if(a.infinity() && b.infinity() && a.sign()!=b.sign()){environment.raise(Invalid);return indefinite();}
        return a.infinity()?a:b;
    }
    if(a.zero() && b.zero())return {0,std::uint16_t((a.sign()==b.sign()?a.sign():environment.rounding()==1)?0x8000:0)};
    if(a.zero() || b.zero()){
        const auto value=a.zero()?b:a;const auto precision=environment.precision();const int exponent=int(value.exponent())-16383;
        if(value.exponent() && exponent>=environment.minimum_exponent && exponent<=environment.maximum_exponent &&
           !(value.significand&((std::uint64_t(1)<<(64-precision))-1)))return value;
        auto n=normalize(value,environment);return pack(value.sign(),n.exponent,Wide(n.significand)<<63,environment);
    }
    if(environment.precision()==24 && normal24(a) && normal24(b)){
        auto x=a,y=b;if(x.exponent()<y.exponent() || (x.exponent()==y.exponent() && x.significand<y.significand))std::swap(x,y);
        const auto difference=x.exponent()-y.exponent();
        if(difference<=32){const auto left=(x.significand>>40)<<difference,right=y.significand>>40;
            const auto magnitude=x.sign()==y.sign()?left+right:left-right;
            if(!magnitude)return {0,std::uint16_t(environment.rounding()==1?0x8000:0)};
            Extended result;if(round24(x.sign(),int(y.exponent())-16383-23,magnitude,environment,result))return result;}
    }
    auto x=normalize(a,environment),y=normalize(b,environment);
    if(x.exponent<y.exponent || (x.exponent==y.exponent && x.significand<y.significand)){std::swap(x,y);std::swap(a,b);}
    const auto left=Wide(x.significand)<<63,right=jam(Wide(y.significand)<<63,unsigned(x.exponent-y.exponent));
    const auto result=a.sign()==b.sign()?left+right:left-right;
    if(!result)return {0,std::uint16_t(environment.rounding()==1?0x8000:0)};
    return pack(a.sign(),x.exponent,result,environment);
}
Extended subtract(Extended a,Extended b,Environment& environment){
    if(a.nan() || b.nan() || a.unsupported() || b.unsupported())return special(a,b,environment);
    b.sign_exponent^=0x8000;return add(a,b,environment);
}
Extended multiply(Extended a,Extended b,Environment& environment){
    environment.status&=~RoundUp;
    if(a.unsupported() || b.unsupported() || a.nan() || b.nan())return special(a,b,environment);
    const bool sign=a.sign()!=b.sign();
    if((a.infinity() && b.zero()) || (b.infinity() && a.zero())){environment.raise(Invalid);return indefinite();}
    denormal_operands(a,b,environment);
    if(a.infinity() || b.infinity())return infinity(sign);
    if(a.zero() || b.zero())return {0,std::uint16_t(sign?0x8000:0)};
    if(environment.precision()==24 && normal24(a) && normal24(b)){
        Extended result;if(round24(sign,int(a.exponent())+int(b.exponent())-2*16383-46,
            (a.significand>>40)*(b.significand>>40),environment,result))return result;
    }
    const auto x=normalize(a,environment),y=normalize(b,environment);
    return pack(sign,x.exponent+y.exponent,Wide(x.significand)*y.significand,environment);
}
Extended divide(Extended a,Extended b,Environment& environment){
    environment.status&=~RoundUp;
    if(a.unsupported() || b.unsupported() || a.nan() || b.nan())return special(a,b,environment);
    const bool sign=a.sign()!=b.sign();
    if((a.infinity() && b.infinity()) || (a.zero() && b.zero())){environment.raise(Invalid);return indefinite();}
    if(b.zero() && !a.infinity()){environment.raise(ZeroDivide);return infinity(sign);}
    denormal_operands(a,b,environment);
    if(a.infinity())return infinity(sign);
    if(b.infinity())return {0,std::uint16_t(sign?0x8000:0)};
    if(b.zero()){environment.raise(ZeroDivide);return infinity(sign);}
    if(a.zero())return {0,std::uint16_t(sign?0x8000:0)};
    const auto precision=environment.precision();
    const auto exact_exponent=int(a.exponent())-int(b.exponent());
    if(a.exponent() && b.exponent() && b.significand==IntegerBit &&
       !(a.significand&((std::uint64_t(1)<<(64-precision))-1)) &&
       exact_exponent>=environment.minimum_exponent && exact_exponent<=environment.maximum_exponent && exact_exponent>=-16382 && exact_exponent<=16383)
        return {a.significand,std::uint16_t((sign?0x8000:0)|(exact_exponent+16383))};
    if(precision==24 && normal24(a) && normal24(b)){
        // Both source operands have 24 significant bits. One 48/24-bit
        // division gives every retained bit and an exact rounding remainder;
        // there is no need to calculate a 127-bit quotient for this case.
        const auto left=a.significand>>40,right=b.significand>>40;const bool below=left<right;
        auto exponent=exact_exponent-int(below);
        if(exponent>=environment.minimum_exponent && exponent<environment.maximum_exponent && exponent>=-16382 && exponent<16383){
            const auto numerator=left<<(below?24:23);auto quotient=numerator/right;const auto remainder=numerator%right;
            const auto rounding=environment.rounding();
            const bool up=remainder && (rounding==1?sign:rounding==2?!sign:rounding==3?false:
                remainder*2>right || (remainder*2==right && (quotient&1)));
            if(remainder)environment.raise(Inexact);if(up){++quotient;environment.raise(RoundUp);}
            if(quotient==(std::uint64_t(1)<<24)){quotient>>=1;++exponent;}
            return {quotient<<40,std::uint16_t((sign?0x8000:0)|(exponent+16383))};
        }
    }
    const auto x=normalize(a,environment),y=normalize(b,environment);
    const auto numerator=Wide(x.significand)<<63;
    const auto high=numerator/y.significand,rem=numerator%y.significand;
    const auto low=(rem<<63)/y.significand,left=(rem<<63)%y.significand;
    return pack(sign,x.exponent-y.exponent,(high<<63)|low|bool(left),environment);
}
Extended square_root(Extended value,Environment& environment){
    environment.status&=~RoundUp;
    if(value.unsupported() || value.nan())return special(value,value,environment);
    if(value.zero())return value;
    if(value.sign()){environment.raise(Invalid);return indefinite();}
    if(value.infinity())return value;
    const auto n=normalize(value,environment);
    const auto parity=unsigned(n.exponent&1),shift=69+parity;
    Wide root{},rem{};
    auto input_bit=[&](int bit){return bit>=int(shift) && bit<int(shift+64)?unsigned((n.significand>>(bit-int(shift)))&1):0u;};
    for(int digit=66;digit>=0;--digit){
        rem=(rem<<2)|(input_bit(digit*2+1)<<1)|input_bit(digit*2);
        const auto trial=(root<<2)|1;
        root<<=1;if(rem>=trial){rem-=trial;root|=1;}
    }
    const int exponent=n.exponent>=0?n.exponent/2:-((-n.exponent+1)/2);
    return pack(false,exponent,(root<<60)|bool(rem),environment);
}
Extended round_integral(Extended value,Environment& environment){
    if(value.unsupported() || value.nan())return special(value,value,environment);
    environment.status&=~RoundUp;
    if(value.infinity() || value.zero() || int(value.exponent())-16383>=63)return value;
    denormal_operands(value,{},environment);
    const auto result=to_integer(value,64,environment);
    auto rounded=from_integer(std::bit_cast<std::int64_t>(result));
    if(rounded.zero())rounded.sign_exponent=value.sign()?0x8000:0;
    return rounded;
}
Extended scale(Extended a,Extended b,Environment& environment){
    environment.status&=~RoundUp;
    if(a.unsupported() || b.unsupported() || a.nan() || b.nan())return special(a,b,environment);
    denormal_operands(a,b,environment);
    if(b.infinity()){
        if((a.zero() && !b.sign()) || (a.infinity() && b.sign())){environment.raise(Invalid);return indefinite();}
        return b.sign()?Extended{0,std::uint16_t(a.sign()?0x8000:0)}:infinity(a.sign());
    }
    if(a.infinity() || a.zero())return a;
    auto temporary=environment;
    int amount;
    if(int(b.exponent())-16383>15)amount=b.sign()?-32768:32768;
    else amount=int(std::bit_cast<std::int64_t>(to_integer(b,64,temporary,true)));
    const auto n=normalize(a,environment);
    return pack(a.sign(),n.exponent+amount,Wide(n.significand)<<63,environment,64);
}
int compare(Extended a,Extended b,Environment& environment,bool ordered){
    if(a.unsupported() || b.unsupported() || a.nan() || b.nan()){
        if(ordered || a.unsupported() || b.unsupported() || a.signaling() || b.signaling())environment.raise(Invalid);
        return 2;
    }
    if(!a.exponent() && a.significand)environment.raise(Denormal);
    if(!b.exponent() && b.significand)environment.raise(Denormal);
    if(a.zero() && b.zero())return 0;
    if(a.sign()!=b.sign())return a.sign()?-1:1;
    const int magnitude=a.exponent()!=b.exponent()?(a.exponent()<b.exponent()?-1:1):a.significand<b.significand?-1:a.significand>b.significand?1:0;
    return a.sign()?-magnitude:magnitude;
}
Extended remainder(Extended a,Extended b,Environment& environment,bool nearest){
    if(a.unsupported() || b.unsupported() || a.nan() || b.nan())return special(a,b,environment);
    if(a.infinity() || b.zero()){environment.raise(Invalid);return indefinite();}
    if(b.infinity() || a.zero()){condition(environment,0);return a;}
    auto x=normalize(a,environment),y=normalize(b,environment);
    const int difference=x.exponent-y.exponent;
    if(difference<-1){condition(environment,0);return a;}
    if(difference>=64){
        const auto reduction=32+(difference&31);
        const auto numerator=Wide(x.significand)<<reduction;
        const auto rem=std::uint64_t(numerator%y.significand);
        auto temporary=environment;
        auto result=pack(a.sign(),x.exponent-reduction,Wide(rem)<<63,temporary,64);
        environment.status|=0x400;return result;
    }
    Wide numerator=x.significand,denominator=y.significand;
    if(difference>=0)numerator<<=difference;else denominator<<=-difference;
    auto quotient=numerator/denominator,rem=numerator%denominator;
    bool negative=a.sign();
    if(nearest && (rem>denominator-rem || (rem==denominator-rem && (quotient&1)))){++quotient;rem=denominator-rem;negative=!negative;}
    condition(environment,unsigned(quotient)&7);
    auto temporary=environment;
    return pack(negative,y.exponent+std::min(0,difference),rem<<63,temporary,64);
}
long double to_long_double(Extended value)noexcept{
    if(value.unsupported() || value.nan())return std::numeric_limits<long double>::quiet_NaN();
    if(value.infinity())return value.sign()?-std::numeric_limits<long double>::infinity():std::numeric_limits<long double>::infinity();
    const auto result=std::ldexp(static_cast<long double>(value.significand),(value.exponent()?int(value.exponent())-16383:-16382)-63);
    return value.sign()?-result:result;
}
Extended from_long_double(long double value,Environment& environment){
    const bool sign=std::signbit(value);
    if(std::isnan(value))return indefinite();
    if(std::isinf(value))return infinity(sign);
    if(value==0)return {0,std::uint16_t(sign?0x8000:0)};
    int exponent{};const auto fraction=std::frexp(std::fabs(value),&exponent);
    // Split conversion avoids converting a value outside uint64_t's range.
    const auto high=std::uint64_t(std::ldexp(fraction,63));
    const auto low=std::uint64_t(std::ldexp(std::ldexp(fraction,63)-static_cast<long double>(high),63));
    return pack(sign,exponent-1,(Wide(high)<<64)|(Wide(low)<<1),environment,64);
}
}
