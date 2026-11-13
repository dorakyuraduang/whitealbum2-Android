#pragma once
#include <winrecomp/runtime.hpp>
namespace wa2::aot {
// The text helpers already hold this exact Memory's ExecutionScope. Keep
// each 32-bit stack operation atomic without reacquiring its TLS ownership.
[[gnu::always_inline]] inline void text_push(wr::Cpu& cpu,wr::Memory::ExecutionScope& scope,wr::U32 value){
    const auto address=cpu.r[wr::ESP]-4;scope.store<32>(address,value);cpu.r[wr::ESP]=address;
}
[[gnu::always_inline]] inline wr::U32 text_pop(wr::Cpu& cpu,wr::Memory::ExecutionScope& scope){
    const auto value=scope.load<32>(cpu.r[wr::ESP]);cpu.r[wr::ESP]+=4;return value;
}
}
