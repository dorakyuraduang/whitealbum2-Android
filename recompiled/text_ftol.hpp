#pragma once
#include <winrecomp/runtime.hpp>
namespace wa2::aot {
// Defaults on. The host's protected, temporary diagnostic control can compare
// the cached body with this admitted path in one unchanged running scene.
void enable_integer_ftol(bool) noexcept;
bool integer_ftol_enabled() noexcept;
std::uint64_t integer_ftol_calls() noexcept;
struct IntegerFtolSource {std::uint32_t entry,feature,store_ip;};
inline constexpr IntegerFtolSource main_integer_ftol_source{0x474f00,0xbc8310,0x474f12};
bool native_integer_ftol(wr::Cpu&,wr::Memory&,wr::Memory::ExecutionScope&,const IntegerFtolSource& = main_integer_ftol_source);
}
