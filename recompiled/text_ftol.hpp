#pragma once
#include <winrecomp/runtime.hpp>
#include "extended80.hpp"
namespace wa2::aot {
// Defaults on. Exact binary64 inputs (including fractional text positions)
// use integer bit operations; other inputs retain the checked source body.
void enable_integer_ftol(bool) noexcept;
bool integer_ftol_enabled() noexcept;
std::uint64_t integer_ftol_calls() noexcept;
bool native_integer_ftol(wr::Cpu&,wr::Memory&,wr::Memory::ExecutionScope&);
struct TextFtolPlan {
    wr::U32 sp{},saved{},operand{},returning{},frame_pointer{},result{},mxcsr{};
    std::uint64_t binary{};
};
bool prepare_text_ftol(wr::Cpu&,wr::Memory::ExecutionScope&,fp::Extended,std::uint16_t,std::uint16_t,bool,TextFtolPlan&);
void apply_text_ftol(wr::Cpu&,wr::Memory::ExecutionScope&,const TextFtolPlan&);
}
