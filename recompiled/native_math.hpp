#pragma once
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
bool native_atan2(wr::Cpu&,wr::Memory&);
bool native_sine(wr::Cpu&,wr::Memory&);
bool native_cosine(wr::Cpu&,wr::Memory&);
}
