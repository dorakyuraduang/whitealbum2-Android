// Only generated native memory operands inside an active, owned arena can
// recover. No other instruction, exception, process or application is handled.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <mutex>
#include "linear_faults.hpp"
namespace wr {
namespace {
struct FaultSite {
    std::uint8_t first[3],recovery[3];
    static std::uint32_t value(const std::uint8_t (&p)[3]) noexcept {
        return p[0]|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16);
    }
};
static_assert(sizeof(FaultSite)==6);
struct ProfilePoint {std::uint32_t offset,tag;};
extern "C" unsigned wa2_native_linear_run(void*,const void*,unsigned);
extern "C" const char wa2_native_linear_code_end[];
extern "C" const FaultSite wa2_native_linear_faults[],wa2_native_linear_faults_end[];
extern "C" const std::uint32_t wa2_native_linear_fault_entry_bytes;
extern "C" const ProfilePoint wa2_native_linear_profile[],wa2_native_linear_profile_end[];
thread_local std::uintptr_t active_base{};
thread_local std::uint64_t recovered{};
LONG CALLBACK recover(EXCEPTION_POINTERS* exception) noexcept {
    const auto* record=exception->ExceptionRecord;
    if(!active_base || record->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || record->NumberParameters<2 || record->ExceptionInformation[0]>1)
        return EXCEPTION_CONTINUE_SEARCH;
    const auto address=record->ExceptionInformation[1];
    if(address<active_base || address-active_base>=0x100010000ull)return EXCEPTION_CONTINUE_SEARCH;
    auto* context=exception->ContextRecord;
    const auto begin=reinterpret_cast<std::uintptr_t>(&wa2_native_linear_run),pc=std::uintptr_t(context->Rip);
    if(pc<begin || pc>=reinterpret_cast<std::uintptr_t>(wa2_native_linear_code_end))return EXCEPTION_CONTINUE_SEARCH;
    const auto offset=std::uint32_t(pc-begin);
    const auto size=(reinterpret_cast<std::uintptr_t>(wa2_native_linear_faults_end)-reinterpret_cast<std::uintptr_t>(wa2_native_linear_faults))/sizeof(FaultSite);
    std::size_t low=0,high=size;
    while(low<high){const auto middle=low+(high-low)/2;if(FaultSite::value(wa2_native_linear_faults[middle].first)<=offset)low=middle+1;else high=middle;}
    if(!low || offset!=FaultSite::value(wa2_native_linear_faults[low-1].first))return EXCEPTION_CONTINUE_SEARCH;
    context->Rip=begin+FaultSite::value(wa2_native_linear_faults[low-1].recovery);++recovered;
    return EXCEPTION_CONTINUE_EXECUTION;
}
}
bool wa2_linear_available(){
    static const bool installed=wa2_native_linear_fault_entry_bytes==sizeof(FaultSite) &&
        reinterpret_cast<std::uintptr_t>(wa2_native_linear_code_end)-reinterpret_cast<std::uintptr_t>(&wa2_native_linear_run)<=0x1000000 &&
        ::AddVectoredExceptionHandler(1,recover)!=nullptr;return installed;
}
unsigned wa2_linear_call(void* state,const void* base,unsigned limit){
    struct Scope {
        std::uintptr_t previous;
        explicit Scope(const void* base):previous(active_base){active_base=reinterpret_cast<std::uintptr_t>(base);}
        ~Scope(){active_base=previous;}
    } scope(base);
    return wa2_native_linear_run(state,base,limit);
}
std::uint64_t wa2_linear_fault_count(){return recovered;}
Wa2NativeCodeLayout wa2_linear_code_layout(){return {
    reinterpret_cast<std::uintptr_t>(&wa2_native_linear_run),reinterpret_cast<std::uintptr_t>(wa2_native_linear_code_end),
    reinterpret_cast<std::uintptr_t>(wa2_native_linear_profile),
    (reinterpret_cast<std::uintptr_t>(wa2_native_linear_profile_end)-reinterpret_cast<std::uintptr_t>(wa2_native_linear_profile))/sizeof(ProfilePoint)};}
}
