#pragma once
#include "winrecomp/x87.hpp"
namespace wr {
inline void sse_execute(Cpu& s,X87Native native,void* operand,bool integer_flags=false) {
    auto result=s.fp;const auto mxcsr=s.fp.get32(24);auto flags=s.flags;
    if(mxcsr&0xffff0000u)throw GuestFault(FaultKind::unsupported,s.eip,"reserved MXCSR bits");
    // SSE has no deferred exception. Detect the new operation's status, then
    // merge sticky bits; an old sticky bit alone must not cause a new exception.
    result.put32(24,(mxcsr|0x1f80u)&~0x3fu);native(result.bytes.data(),operand,&flags);
    const auto raised=result.get32(24)&0x3fu;
    if(raised&~(mxcsr>>7)&0x3fu)throw GuestFault(FaultKind::floating,s.eip,"unmasked SIMD exception; resumable delivery not implemented");
    result.put32(24,mxcsr|raised);s.fp=result;
    if(integer_flags){s.flags=(s.flags&~STATUS_FLAGS)|(flags&STATUS_FLAGS);s.defined_flags|=STATUS_FLAGS;}
}
inline void sse_memory(Cpu& s,Memory& m,X87Native native,U32 address,unsigned size,bool store,unsigned alignment,bool integer_flags=false) {
    if(size!=4 && size!=8 && size!=16)throw GuestFault(FaultKind::unsupported,s.eip,"unsupported SSE memory width");
    if(alignment && address%alignment)throw GuestFault(FaultKind::memory,address,"SSE aligned operand is misaligned");
    auto guard=m.lock();m.check(address,size,store?Memory::Write:Memory::Read);
    alignas(16) std::array<std::uint8_t,16> operand{};
    if(!store)m.copy_out(address,std::span(operand.data(),size));
    sse_execute(s,native,operand.data(),integer_flags);
    if(store)m.copy_in(address,std::span(operand.data(),size));
}
}
