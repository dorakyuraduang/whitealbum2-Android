#pragma once
#include <winrecomp/runtime.hpp>
namespace wa2::aot {
// Defaults on. The host's protected, temporary diagnostic control can compare
// the cached body with this admitted path in one unchanged running scene.
void enable_integer_ftol(bool) noexcept;
bool integer_ftol_enabled() noexcept;
std::uint64_t integer_ftol_calls() noexcept;
bool native_integer_ftol(wr::Cpu&,wr::Memory&,wr::Memory::ExecutionScope&);
}
