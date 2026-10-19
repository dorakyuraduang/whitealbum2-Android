#pragma once
#include "winrecomp/integer.hpp"
namespace wr {
inline unsigned xmm_offset(unsigned index) {
    if(index>=8)throw std::runtime_error("invalid guest XMM register");
    return 160+index*16;
}
inline U32 xmm_dword(const Cpu& s,unsigned index) {return s.fp.get32(xmm_offset(index));}
inline U32 xmm_word(const Cpu& s,unsigned index,unsigned lane) {return s.fp.get16(xmm_offset(index)+(lane&7u)*2);}
inline void xmm_insert_word(Cpu& s,unsigned index,U32 value,unsigned lane) {s.fp.put16(xmm_offset(index)+(lane&7u)*2,std::uint16_t(value));}
inline void xmm_from_dword(Cpu& s,unsigned index,U32 value) {
    const auto at=xmm_offset(index);s.fp.put64(at,value);s.fp.put64(at+8,0);
}
inline U32 xmm_byte_mask(const Cpu& s,unsigned index) {
    const auto at=xmm_offset(index);U32 value=0;
    for(unsigned i=0;i<16;++i)value|=U32(s.fp.bytes[at+i]>>7)<<i;
    return value;
}
inline void xmm_shift_qwords(Cpu& s,unsigned index,unsigned count,bool left) {
    const auto at=xmm_offset(index);
    for(unsigned lane=0;lane<2;++lane) {
        auto value=s.fp.get64(at+lane*8);
        value=count>=64?0:left?value<<count:value>>count;s.fp.put64(at+lane*8,value);
    }
}
inline void xmm_shuffle(Cpu& s,unsigned destination,const std::array<U32,4>& source,unsigned control) {
    const auto at=xmm_offset(destination);
    for(unsigned lane=0;lane<4;++lane)s.fp.put32(at+lane*4,source[(control>>(lane*2))&3u]);
}
inline void xmm_shuffle_reg(Cpu& s,unsigned destination,unsigned source,unsigned control) {
    const auto at=xmm_offset(source);
    const std::array<U32,4> value{s.fp.get32(at),s.fp.get32(at+4),s.fp.get32(at+8),s.fp.get32(at+12)};
    xmm_shuffle(s,destination,value,control);
}
inline void xmm_shuffle_memory(Cpu& s,Memory& m,unsigned destination,U32 address,unsigned control) {
    if(address%16)throw GuestFault(FaultKind::memory,address,"PSHUFD memory is misaligned");
    auto lock=m.lock();m.check(address,16,Memory::Read);
    const std::array<U32,4> value{m.load(address,32),m.load(address+4,32),m.load(address+8,32),m.load(address+12,32)};
    xmm_shuffle(s,destination,value,control);
}
inline void xmm_shuffle_pd_reg(Cpu& s,unsigned destination,unsigned source,unsigned control) {
    const auto at=xmm_offset(destination),from=xmm_offset(source);
    // Capture both lanes before committing; source and destination may alias.
    const auto low=s.fp.get64(at+(control&1u)*8),high=s.fp.get64(from+((control>>1)&1u)*8);
    s.fp.put64(at,low);s.fp.put64(at+8,high);
}
inline void xmm_shuffle_pd_memory(Cpu& s,Memory& m,unsigned destination,U32 address,unsigned control) {
    if(address%16)throw GuestFault(FaultKind::memory,address,"SHUFPD memory is misaligned");
    auto lock=m.lock();m.check(address,16,Memory::Read);
    const auto from=address+((control>>1)&1u)*8;
    const std::uint64_t high=std::uint64_t(m.load(from,32))|(std::uint64_t(m.load(from+4,32))<<32);
    const auto at=xmm_offset(destination);const auto low=s.fp.get64(at+(control&1u)*8);
    s.fp.put64(at,low);s.fp.put64(at+8,high);
}
}
