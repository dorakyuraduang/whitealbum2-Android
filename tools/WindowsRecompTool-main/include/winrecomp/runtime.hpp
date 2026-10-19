#pragma once
#include "winrecomp/memory.hpp"
#include "winrecomp/fp_state.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace wr {
using U32=std::uint32_t;
constexpr U32 CF=1, PF=4, AF=16, ZF=64, SF=128, OF=2048, STATUS_FLAGS=CF|PF|AF|ZF|SF|OF;
enum RegisterIndex { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };
struct Cpu {
    std::array<U32,8> r{};
    U32 eip{}, flags{0x202}, defined_flags{STATUS_FLAGS};
    U32 fs_base{}, gs_base{};
    bool fs_valid{}, gs_valid{};
    // Flat 32-bit Windows guest selectors, ordered ES,CS,SS,DS,FS,GS.
    std::array<std::uint16_t,6> selectors{0x2b,0x23,0x2b,0x2b,0x53,0};
    FpState fp;
};
using External=bool(*)(Cpu&,Memory&,void*);
constexpr U32 DF=0x400;
inline U32 segment_address(const Cpu& s,U32 offset,unsigned segment) {
    if(!segment)return offset;
    if(segment==1 && s.fs_valid)return s.fs_base+offset;
    if(segment==2 && s.gs_valid)return s.gs_base+offset;
    throw GuestFault(FaultKind::unsupported,s.eip,"uninitialized guest segment");
}
inline U32 mask(unsigned width) { if(width!=8 && width!=16 && width!=32)throw std::runtime_error("unsupported integer width");return width==32?0xffffffffu:(U32(1)<<width)-1; }
inline U32 reg(const Cpu& s,unsigned index,unsigned width=32,unsigned shift=0) { return (s.r.at(index)>>shift)&mask(width); }
inline void set_reg(Cpu& s,unsigned index,U32 value,unsigned width=32,unsigned shift=0) { const auto m=mask(width)<<shift;s.r.at(index)=(s.r.at(index)&~m)|((value<<shift)&m); }
inline U32 sign_extend(U32 v,unsigned width) { v&=mask(width);const U32 sign=U32(1)<<(width-1);return (v^sign)-sign; }
inline U32 szp(U32 value,unsigned width) {
    value&=mask(width);
    return (value==0?ZF:0) | (value&(U32(1)<<(width-1))?SF:0) | ((std::popcount(value&0xffu)&1)==0?PF:0);
}
inline U32 add(Cpu& s,U32 a,U32 b,unsigned width,U32 carry=0) {
    const U32 m=mask(width), sign=U32(1)<<(width-1);a&=m;b&=m;carry&=1;
    const std::uint64_t wide=std::uint64_t(a)+b+carry;const U32 r=U32(wide)&m;
    const U32 f=szp(r,width) | (wide>m?CF:0) | ((~(a^b)&(a^r)&sign)?OF:0) | ((a^b^r)&AF);
    s.flags=(s.flags&~STATUS_FLAGS)|f;s.defined_flags|=STATUS_FLAGS;return r;
}
inline U32 sub(Cpu& s,U32 a,U32 b,unsigned width,U32 borrow=0) {
    const U32 m=mask(width), sign=U32(1)<<(width-1);a&=m;b&=m;borrow&=1;
    const std::uint64_t rhs=std::uint64_t(b)+borrow;const U32 r=U32(std::uint64_t(a)-rhs)&m;
    const U32 f=szp(r,width) | (std::uint64_t(a)<rhs?CF:0) | (((a^b)&(a^r)&sign)?OF:0) | ((a^b^r)&AF);
    s.flags=(s.flags&~STATUS_FLAGS)|f;s.defined_flags|=STATUS_FLAGS;return r;
}
inline U32 logic(Cpu& s,U32 r,unsigned width) {
    r&=mask(width);s.flags=(s.flags&~(CF|PF|ZF|SF|OF))|szp(r,width);s.defined_flags=(s.defined_flags|STATUS_FLAGS)&~AF;return r;
}
inline U32 incdec(Cpu& s,U32 a,unsigned width,bool decrement) {
    const auto carry=s.flags&CF, defined=s.defined_flags&CF;
    const auto r=decrement?sub(s,a,1,width):add(s,a,1,width);
    s.flags=(s.flags&~CF)|carry;s.defined_flags=(s.defined_flags&~CF)|defined;return r;
}
inline void push(Cpu& s,Memory& m,U32 v,unsigned width=32) { const auto p=s.r[ESP]-width/8;m.store(p,v,width);s.r[ESP]=p; }
inline U32 pop(Cpu& s,Memory& m,unsigned width=32) { const auto v=m.load(s.r[ESP],width);s.r[ESP]+=width/8;return v; }
}
