#include "scene_builtins.hpp"
#include "scalar_math.hpp"
#include <cmath>

namespace wa2::script {
std::optional<CommandResult> SceneBuiltins::dispatch(Command& command){
    const auto id=command.index;
    if(id!=4 && (id<8 || id>17) && id!=128 && id!=129)return {};
    const auto expected=id==15 || id==16 || id==129?2u:1u;
    if(command.arguments.size()!=expected)throw Error("Invalid native script builtin arguments");
    if(id==4 || id==128 || id==129){
        if(diagnostic_){const auto& v=command.arguments[0];diagnostic_(v.type()==Type::text?v.text():v.type()==Type::real?std::to_string(v.real()):std::to_string(v.integer()));}
        return CommandResult{true,false,Value{}};
    }
    if(id==8){const auto n=command.arguments[0].integer();return CommandResult{true,false,Value(n<=0?0:std::int32_t(random_.next()%std::uint32_t(n)))};}
    const auto x=command.arguments[0].real();double result{};
    // Cached source constants, including the intermediate binary32 store.
    constexpr double radians=0.01745329238474369,degrees=57.2957763671875;
    const auto angle=float(double(x)*radians);
    switch(id){
    case 9:result=std::isfinite(angle)?math::sine_finite(angle).value:std::sin(double(angle));break;
    case 10:result=std::isfinite(angle)?math::cosine_finite(angle).value:std::cos(double(angle));break;
    case 11:result=std::tan(double(angle));break;
    case 12:result=double(float(std::asin(double(x))))*degrees;break;
    case 13:result=double(float(std::acos(double(x))))*degrees;break;
    case 14:result=double(float(std::atan(double(x))))*degrees;break;
    case 15:result=double(float(std::atan2(double(x),double(command.arguments[1].real()))))*degrees;break;
    case 16:result=std::pow(double(x),double(command.arguments[1].real()));break;
    case 17:result=std::sqrt(double(x));break;
    default:throw Error("Unknown native script builtin");
    }
    return CommandResult{true,false,Value(float(result))};
}
}
