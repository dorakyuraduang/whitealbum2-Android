#pragma once
#include "winrecomp/integer.hpp"
namespace wr {
using X87Native=void(*)(void*,void*,U32*);
inline void x87_wait(const Cpu& s) {
    if((s.fp.get16(2)&~s.fp.get16(0))&0x3fu)throw GuestFault(FaultKind::floating,s.eip,"pending guest x87 exception; SEH delivery is not implemented");
}
inline void x87_execute(Cpu& s,X87Native native,void* operand,unsigned opcode,bool control=false,bool integer_flags=false,U32 data_address=0,bool has_memory=false) {
    x87_wait(s);
    auto result=s.fp;const auto control_word=s.fp.get16(0);auto flags=s.flags;
    // Never expose the host process to a guest's unmasked exception. The first
    // unmasked operation stops explicitly, before committing its data/registers.
    if(!control)result.put16(0,std::uint16_t(control_word|0x3fu));
    native(result.bytes.data(),operand,&flags);
    if(!control)result.put16(0,control_word);
    if(!control && (result.get16(2)&~control_word&0x3fu))
        throw GuestFault(FaultKind::floating,s.eip,"unmasked x87 exception; resumable exception delivery not implemented");
    // Do not let saved host instruction/data pointers leak into guest state.
    if(control) {
        result.put64(8,s.fp.get64(8));result.put64(16,s.fp.get64(16));result.put16(6,s.fp.get16(6));
    }else {
        result.put64(8,s.eip);result.put16(6,std::uint16_t(opcode&0x7ffu));
        result.put64(16,has_memory?data_address:s.fp.get64(16));
    }
    s.fp=result;
    if(integer_flags){s.flags=(s.flags&~STATUS_FLAGS)|(flags&STATUS_FLAGS);s.defined_flags|=STATUS_FLAGS;}
}
inline void x87_memory(Cpu& s,Memory& m,X87Native native,U32 address,unsigned size,bool store,unsigned opcode,bool control=false) {
    x87_wait(s);auto guard=m.lock();
    if(!size || size>10)throw GuestFault(FaultKind::unsupported,s.eip,"unsupported native x87 operand size");
    m.check(address,size,store?Memory::Write:Memory::Read);
    alignas(16) std::array<std::uint8_t,16> operand{};
    if(!store)m.copy_out(address,std::span(operand.data(),size));
    x87_execute(s,native,operand.data(),opcode,control,false,address,true);
    if(store)m.copy_in(address,std::span(operand.data(),size));
}
inline void x87_clear(Cpu& s){s.fp.put16(2,std::uint16_t(s.fp.get16(2)&0x7f00u));}
inline void x87_init(Cpu& s) {
    s.fp.put16(0,0x037f);s.fp.put16(2,0);s.fp.bytes[4]=0;s.fp.put16(6,0);s.fp.put64(8,0);s.fp.put64(16,0);
}
inline void x87_env(Cpu& s,Memory& m,U32 address,bool restore,bool registers) {
    const unsigned size=registers?108u:28u;auto guard=m.lock();
    std::array<std::uint8_t,108> buffer{};
    auto put16=[&](unsigned off,std::uint16_t v){buffer[off]=std::uint8_t(v);buffer[off+1]=std::uint8_t(v>>8);};
    auto put32=[&](unsigned off,U32 v){put16(off,std::uint16_t(v));put16(off+2,std::uint16_t(v>>16));};
    auto get16=[&](unsigned off){return std::uint16_t(U32(buffer[off])|(U32(buffer[off+1])<<8));};
    auto get32=[&](unsigned off){return U32(get16(off))|(U32(get16(off+2))<<16);};
    if(restore) {
        m.copy_out(address,std::span(buffer.data(),size));
        // This flat guest profile stores zero FCS/FDS. Do not silently discard
        // nonzero legacy selector state supplied by a different environment.
        if(get16(16) || get16(24))throw GuestFault(FaultKind::unsupported,s.eip,"nonzero x87 environment selectors require a segmented guest profile");
        auto next=s.fp;
        next.put16(0,get16(0));next.put16(2,get16(4));next.bytes[4]=0;const auto tag=get16(8);
        for(unsigned k=0;k<8;++k)if(((tag>>(k*2))&3u)!=3u)next.bytes[4]|=std::uint8_t(1u<<k);
        next.put64(8,get32(12));next.put16(6,std::uint16_t(get16(18)&0x7ff));next.put64(16,get32(20));
        if(registers)for(unsigned k=0;k<8;++k)std::copy_n(buffer.data()+28+k*10,10,next.bytes.data()+32+k*16);
        else {
            // FXSAVE serializes logical ST(i); FLDENV changes TOP without
            // moving physical registers. Rotate our logical serialization.
            const auto old_top=s.fp.top(),new_top=next.top();
            for(unsigned k=0;k<8;++k)std::copy_n(s.fp.bytes.data()+32+((new_top+k-old_top)&7u)*16,16,next.bytes.data()+32+k*16);
        }
        if(next.full_tag()!=tag)throw GuestFault(FaultKind::unsupported,s.eip,"x87 environment has noncanonical full tags not representable by the current native backend");
        s.fp=next;
    }else {
        put32(0,0xffff0000u|s.fp.get16(0));put32(4,0xffff0000u|s.fp.get16(2));put32(8,0xffff0000u|s.fp.full_tag());
        put32(12,U32(s.fp.get64(8)));put16(16,0);put16(18,s.fp.get16(6));put32(20,U32(s.fp.get64(16)));put32(24,0xffff0000u);
        if(registers)for(unsigned k=0;k<8;++k)std::copy_n(s.fp.bytes.data()+32+k*16,10,buffer.data()+28+k*10);
        m.copy_in(address,std::span(buffer.data(),size));
        if(registers)x87_init(s);else s.fp.put16(0,std::uint16_t(s.fp.get16(0)|0x3f));
    }
}
}
