#pragma once
#include <winrecomp/runtime.hpp>
namespace wr {
struct Wa2NativeInstruction {U32 pc;unsigned length;};
struct Wa2NativeForwardRegion {U32 pc;unsigned maximum;};
std::span<const U32> wa2_native_entries();
std::span<const Wa2NativeForwardRegion> wa2_native_forward_regions();
std::span<const Wa2NativeInstruction> wa2_native_instructions();
bool wa2_native_entry(U32);
bool wa2_native_code_verified(Memory&,U32);
void wa2_native_profiling(bool);
std::uint64_t wa2_native_inline_state_api_calls();
unsigned wa2_native_batch(Cpu&,Memory&,std::uint64_t&,unsigned,U32);
std::string wa2_native_report();
}
