#pragma once
#include "winrecomp/integer.hpp"
namespace wr {
inline void check_code(Memory& memory,U32 pc,std::initializer_list<std::uint8_t> expected) {
    if(expected.size()>15)throw std::runtime_error("invalid generated instruction length");
    if(!memory.matches_code(pc,std::span(expected.begin(),expected.size())))throw GuestFault(FaultKind::unsupported,pc,"self-modified code differs from its static translation");
}
}
