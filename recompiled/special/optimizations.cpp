#include "optimizations.hpp"
#include "optimization_profile.hpp"
#include "../text_ftol.hpp"
#include "../native_math.hpp"
namespace wa2::special {
namespace {thread_local std::uint64_t conversions{},sines{},cosines{},atans{};}
bool scalar_optimization(wr::Cpu& cpu,wr::Memory& memory,wr::Memory::ExecutionScope& scope){
    if(optimization_reference)return false;
    try{
    if(cpu.eip==integer_ftol_source.entry){const auto hit=aot::native_integer_ftol(cpu,memory,scope,integer_ftol_source);conversions+=hit;return hit;}
    if(cpu.eip==sine_source.entry){const auto hit=aot::native_sine(cpu,memory,sine_source);sines+=hit;return hit;}
    if(cpu.eip==cosine_source.entry){const auto hit=aot::native_cosine(cpu,memory,cosine_source);cosines+=hit;return hit;}
    if(cpu.eip==atan2_entry){const auto hit=aot::native_atan2(cpu,memory,atan2_entry);atans+=hit;return hit;}
    }catch(const wr::GuestFault&){return false;}
    return false;
}
std::string scalar_optimization_report(){return "{\"integer_conversions\":"+std::to_string(conversions)+",\"sines\":"+std::to_string(sines)+
    ",\"cosines\":"+std::to_string(cosines)+",\"atan2\":"+std::to_string(atans)+",\"shared_main_kernels\":true}";}
std::uint64_t scalar_optimization_activity(){return conversions+sines+cosines+atans;}
}
