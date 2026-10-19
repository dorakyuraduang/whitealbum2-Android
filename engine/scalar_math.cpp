#include "scalar_math.hpp"
#include "math_environment.hpp"
#include <cmath>
#pragma STDC FENV_ACCESS ON
namespace wa2::math {
ScalarResult atan2_finite(double y,double x){
    if(!detail::finite(y) || !detail::finite(x))throw std::invalid_argument("Native atan2 requires finite source arguments");
    detail::NativeEnvironment scope;
    // The source CRT documents a zero result when both inputs are zero.
    const auto result=(y==0 && x==0)?std::copysign(0.,y):std::atan2(y,x);
    return scope.finish(result);
}
}
