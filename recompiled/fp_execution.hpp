#pragma once
#include <winrecomp/sse_core.hpp>
#include "x87_kernel.hpp"
namespace wa2::aot::fp {
namespace detail {
// The enclosing execution scope keeps page metadata and backing bytes stable.
// Normal floating operands need one sparse-page lookup rather than a region
// tree validation followed by another lookup for the copy. Faulting/cross-page
// operands retain Memory's precise, atomic checked accessors.
inline const wr::Memory::NativePage* operand_page(const wr::Memory::ExecutionScope& scope,wr::U32 address,unsigned size,unsigned access){
    const auto* index=scope.native_pages()[address>>22];if(!index)return nullptr;
    const auto& page=(*index)[(address>>12)&1023];const auto offset=address&4095;
    return page.data && (page.permissions&access)==access && offset<page.length && size<=page.length-offset?&page:nullptr;
}
inline void read_operand(wr::Memory& memory,const wr::Memory::ExecutionScope& scope,wr::U32 address,void* output,unsigned size){
    if(const auto* page=operand_page(scope,address,size,wr::Memory::Read))std::memcpy(output,page->data+(address&4095),size);
    else memory.copy_out(address,{static_cast<std::uint8_t*>(output),size});
}
inline void check_store_operand(wr::Memory& memory,const wr::Memory::ExecutionScope& scope,wr::U32 address,unsigned size){
    if(!operand_page(scope,address,size,wr::Memory::Write))memory.check(address,size,wr::Memory::Write);
}
}
// Only statically selected portable kernels use these templates. Normal game
// arithmetic masks exceptions, so no rollback image or 512-byte state copy is
// needed. Unmasked operations retain the transactional, checked ABI wrapper.
template<auto Kernel>
inline void x87_execute(wr::Cpu& s,void* operand,unsigned opcode,bool control=false,
                        bool integer_flags=false,wr::U32 data_address=0,bool has_memory=false){
    if constexpr(Kernel==x87<217,5,true>){if(control){
        wr::x87_wait(s);
        const auto word=std::uint16_t((detail::read_word(operand,2)&0x1f3f)|0x40);auto status=s.fp.get16(2);
        if(status&~word&0x3f)status|=0x8080;else status&=~0x8080;
        s.fp.put16(0,word);s.fp.put16(2,status);s.fp.clear_x87_padding();s.fp.put32(28,0xffff);
        if(integer_flags)s.defined_flags|=wr::STATUS_FLAGS;return;
    }}
    const auto cw=s.fp.get16(0);
    if(control || (cw&0x3f)!=0x3f){
        wr::x87_execute(s,Kernel,operand,opcode,control,integer_flags,data_address,has_memory);return;
    }
    wr::x87_wait(s);auto flags=s.flags;const auto previous_data=s.fp.get64(16);
    Kernel(s.fp.bytes.data(),operand,&flags);
    s.fp.put64(8,s.eip);s.fp.put16(6,std::uint16_t(opcode&0x7ff));
    s.fp.put64(16,has_memory?data_address:previous_data);
    if(integer_flags){s.flags=(s.flags&~wr::STATUS_FLAGS)|(flags&wr::STATUS_FLAGS);s.defined_flags|=wr::STATUS_FLAGS;}
}
template<auto Kernel>
inline void x87_memory(wr::Cpu& s,wr::Memory& m,wr::U32 address,unsigned size,bool store,
                       unsigned opcode,bool control=false){
    wr::x87_wait(s);auto scope=m.execution_scope();
    if(!size || size>10)throw wr::GuestFault(wr::FaultKind::unsupported,s.eip,"unsupported native x87 operand size");
    alignas(16) std::array<std::uint8_t,16> operand{};
    if(store)detail::check_store_operand(m,scope,address,size);
    else detail::read_operand(m,scope,address,operand.data(),size);
    x87_execute<Kernel>(s,operand.data(),opcode,control,false,address,true);
    if(store)scope.write_bytes(address,std::span(operand.data(),size));
}
template<auto Kernel>
inline void sse_execute(wr::Cpu& s,void* operand,bool integer_flags=false){
    const auto mxcsr=s.fp.get32(24);
    if((mxcsr&0xffff1f80u)!=0x1f80u){wr::sse_execute(s,Kernel,operand,integer_flags);return;}
    auto flags=s.flags;s.fp.put32(24,mxcsr&~0x3fu);
    Kernel(s.fp.bytes.data(),operand,&flags);
    s.fp.put32(24,mxcsr|(s.fp.get32(24)&0x3f));
    if(integer_flags){s.flags=(s.flags&~wr::STATUS_FLAGS)|(flags&wr::STATUS_FLAGS);s.defined_flags|=wr::STATUS_FLAGS;}
}
template<auto Kernel>
inline void sse_memory(wr::Cpu& s,wr::Memory& m,wr::U32 address,unsigned size,bool store,unsigned alignment,bool integer_flags=false){
    if(size!=4 && size!=8 && size!=16)throw wr::GuestFault(wr::FaultKind::unsupported,s.eip,"unsupported SSE memory width");
    if(alignment && address%alignment)throw wr::GuestFault(wr::FaultKind::memory,address,"SSE aligned operand is misaligned");
    auto scope=m.execution_scope();
    alignas(16) std::array<std::uint8_t,16> operand{};
    if(store)detail::check_store_operand(m,scope,address,size);
    else detail::read_operand(m,scope,address,operand.data(),size);
    sse_execute<Kernel>(s,operand.data(),integer_flags);
    if(store)scope.write_bytes(address,std::span(operand.data(),size));
}
template<auto Kernel>inline void sse_i32_to_sd(wr::Cpu& s,wr::U32 input){
    alignas(16) std::array<std::uint8_t,16> operand{};
    for(unsigned i=0;i<4;++i)operand[i]=std::uint8_t(input>>(i*8));
    sse_execute<Kernel>(s,operand.data());
}
template<auto Kernel>inline wr::U32 sse_sd_to_i32(wr::Cpu& s){
    alignas(16) std::array<std::uint8_t,16> operand{};sse_execute<Kernel>(s,operand.data());
    return wr::U32(operand[0])|(wr::U32(operand[1])<<8)|(wr::U32(operand[2])<<16)|(wr::U32(operand[3])<<24);
}
template<auto Kernel>inline wr::U32 sse_sd_memory_to_i32(wr::Cpu& s,wr::Memory& m,wr::U32 address){
    auto scope=m.execution_scope();
    alignas(16) std::array<std::uint8_t,16> operand{};detail::read_operand(m,scope,address,operand.data(),8);
    sse_execute<Kernel>(s,operand.data());
    return wr::U32(operand[0])|(wr::U32(operand[1])<<8)|(wr::U32(operand[2])<<16)|(wr::U32(operand[3])<<24);
}
}
