#pragma once
#include <cstdint>
namespace wr {struct Cpu;class Memory;}
namespace wa2::aot {
#if WA2_AOT_DIAGNOSTICS
extern thread_local bool native_math_ports_enabled;
struct NativeMathTestCounts {unsigned long long atan2{},sine{},cosine{};};
extern thread_local NativeMathTestCounts native_math_test_counts;
// Full raw-state AOT comparisons exercise the original function body.
// Native ports have separate public-ABI and numeric-result checks.
class NativeMathReferenceScope {
    bool saved_;
public:
    NativeMathReferenceScope():saved_(native_math_ports_enabled){native_math_ports_enabled=false;}
    ~NativeMathReferenceScope(){native_math_ports_enabled=saved_;}
};
#endif
// Source CRT function ABI, not an x87 instruction implementation. Declining
// a contract leaves source state untouched and retains the checked AOT path.
struct TrigonometrySource {std::uint32_t entry,feature,table;};
inline constexpr TrigonometrySource main_sine_source{0x474d50,0xbc830c,0x4ac710};
inline constexpr TrigonometrySource main_cosine_source{0x474990,0xbc830c,0x4a84d0};
bool native_atan2(wr::Cpu&,wr::Memory&,std::uint32_t entry=0x47885a);
bool native_sine(wr::Cpu&,wr::Memory&,const TrigonometrySource& = main_sine_source);
bool native_cosine(wr::Cpu&,wr::Memory&,const TrigonometrySource& = main_cosine_source);
}
