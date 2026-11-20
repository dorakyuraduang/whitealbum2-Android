#include "extended80.hpp"
#include <stdexcept>
#include "../third_party/x87_math/fpu/fpu_trans.h"
namespace wa2::aot::fp::detail {
Extended transcend(unsigned operation,Extended first,Extended second,Environment& e){
    softfloat_status_t status{};status.softfloat_roundingMode=e.rounding();status.softfloat_exceptionMasks=e.control&0x3f;
    status.extF80_roundingPrecision=80;
    floatx80 a{first.significand,first.sign_exponent},b{second.significand,second.sign_exponent},r{};
    switch(operation){
    case 0xf0:r=f2xm1(a,status);break;
    case 0xf1:r=fyl2x(a,b,status);break;
    case 0xf9:r=fyl2xp1(a,b,status);break;
    case 0xf3:r=fpatan(a,b,status);break;
    case 0xfe:fsin(a,status);r=a;break;
    case 0xff:fcos(a,status);r=a;break;
    case 0xf2:fptan(a,status);r=a;break;
    default:throw std::logic_error("unknown static transcendental operation");
    }
    e.status=std::uint16_t((e.status&~RoundUp)|(status.softfloat_exceptionFlags&(0x3f|RoundUp)));
    return {r.signif,r.signExp};
}
}
