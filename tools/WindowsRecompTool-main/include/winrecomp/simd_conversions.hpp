#pragma once
#include "winrecomp/sse_core.hpp"
namespace wr {
inline void sse_i32_to_sd(Cpu& s,X87Native native,U32 input) {
    alignas(16) std::array<std::uint8_t,16> operand{};
    for(unsigned i=0;i<4;++i)operand[i]=std::uint8_t(input>>(i*8));
    sse_execute(s,native,operand.data());
}
inline U32 sse_sd_to_i32(Cpu& s,X87Native native) {
    alignas(16) std::array<std::uint8_t,16> operand{};
    sse_execute(s,native,operand.data());
    return U32(operand[0])|(U32(operand[1])<<8)|(U32(operand[2])<<16)|(U32(operand[3])<<24);
}
inline U32 sse_sd_memory_to_i32(Cpu& s,Memory& m,X87Native native,U32 address) {
    auto lock=m.lock();m.check(address,8,Memory::Read);
    alignas(16) std::array<std::uint8_t,16> operand{};m.copy_out(address,std::span(operand.data(),8));
    sse_execute(s,native,operand.data());
    return U32(operand[0])|(U32(operand[1])<<8)|(U32(operand[2])<<16)|(U32(operand[3])<<24);
}
}
