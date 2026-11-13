#include "text_ftol.hpp"
#include "fp_execution.hpp"
#include <atomic>
#include <cstring>

namespace wa2::aot {namespace {
std::atomic<bool> enabled{true};
std::atomic<std::uint64_t> calls{};
bool overlaps(const void* a,unsigned na,const void* b,unsigned nb){
    const auto x=reinterpret_cast<std::uintptr_t>(a),y=reinterpret_cast<std::uintptr_t>(b);
    return x<=y?y-x<na:x-y<nb;
}
}
void enable_integer_ftol(bool value)noexcept{enabled.store(value,std::memory_order_relaxed);}
bool integer_ftol_enabled()noexcept{return enabled.load(std::memory_order_relaxed);}
std::uint64_t integer_ftol_calls()noexcept{return calls.load(std::memory_order_relaxed);}
bool prepare_text_ftol(wr::Cpu& s,wr::Memory::ExecutionScope& scope,fp::Extended value,std::uint16_t cw,std::uint16_t status,bool present,TextFtolPlan& plan){
    if(s.eip!=0x474f00 || !integer_ftol_enabled())return false;
    if((cw&0x3f)!=0x3f || (status&0x8080) || !present ||
       (s.fp.get32(24)&0xffff1f80u)!=0x1f80u)return false;
    std::uint32_t magnitude{};std::uint64_t binary=std::uint64_t(value.sign())<<63;bool fractional{};
    if(!value.zero()){
        const int exponent=int(value.exponent())-16383;
        // Text layout uses PC24 intermediates, including fractional positions.
        // Admit every normal binary64-exact input in range. No host floating
        // operation or rounding mode is involved; wider inputs retain FSTP.
        if(exponent < -1022 || exponent>31 || !(value.significand>>63) || (value.significand&0x7ffu))return false;
        const unsigned shift=exponent<0?0:63-unsigned(exponent);
        const auto absolute=exponent<0?0:value.significand>>shift;
        fractional=exponent<0 || (value.significand&((std::uint64_t(1)<<shift)-1));
        if(absolute>(value.sign()?0x80000000ull:0x7fffffffull))return false;
        magnitude=std::uint32_t(absolute);
        binary|=(std::uint64_t(exponent+1023)<<52)|((value.significand>>11)&0xfffffffffffffull);
    }
    const auto sp=s.r[wr::ESP];if(sp<32)return false;
    const auto saved=sp-4,operand=(sp-12)&~7u;
    const auto* feature=fp::detail::operand_page(scope,0xbc8310,4,wr::Memory::Read);
    const auto* ret=fp::detail::operand_page(scope,sp,4,wr::Memory::Read);
    const auto* save=fp::detail::operand_page(scope,saved,4,wr::Memory::Read|wr::Memory::Write);
    const auto* data=fp::detail::operand_page(scope,operand,8,wr::Memory::Read|wr::Memory::Write);
    if(!feature || !ret || !save || !data || ((save->permissions|data->permissions)&wr::Memory::Execute))return false;
    const auto* rp=ret->data+(sp&4095);const auto* bp=save->data+(saved&4095);const auto* dp=data->data+(operand&4095);
    if(overlaps(rp,4,bp,4) || overlaps(rp,4,dp,8) || overlaps(bp,4,dp,8))return false;
    if(!scope.load<32>(0xbc8310))return false;
    const auto returning=scope.load<32>(sp),old_bp=s.r[wr::EBP];
    plan={sp,saved,operand,returning,old_bp,value.sign()?0u-magnitude:magnitude,s.fp.get32(24)|(fractional?0x20u:0u),binary};
    return true;
}
void apply_text_ftol(wr::Cpu& s,wr::Memory::ExecutionScope& scope,const TextFtolPlan& plan){
    // These are the exact source stack stores and final arithmetic flags.
    // Earlier CMP flags are overwritten before any later use or fault.
    scope.store<32>(plan.saved,plan.frame_pointer);s.r[wr::EBP]=plan.saved;s.r[wr::ESP]=plan.saved;
    s.r[wr::ESP]=wr::sub(s,s.r[wr::ESP],8,32);
    s.r[wr::ESP]=wr::logic(s,s.r[wr::ESP]&~7u,32);
    std::array<std::uint8_t,8> bytes;fp::detail::write_word(bytes.data(),plan.binary,8);
    scope.write_bytes(plan.operand,bytes);s.fp.put32(24,plan.mxcsr);
    s.r[wr::EAX]=plan.result;
    s.r[wr::EBP]=plan.frame_pointer;s.r[wr::ESP]=plan.sp+4;s.eip=plan.returning;
    calls.fetch_add(1,std::memory_order_relaxed);
}
bool native_integer_ftol(wr::Cpu& s,wr::Memory&,wr::Memory::ExecutionScope& scope){
    const auto cw=s.fp.get16(0),status=s.fp.get16(2);const unsigned top=(status>>11)&7;
    TextFtolPlan plan;
    if(!prepare_text_ftol(s,scope,{s.fp.get64(32),s.fp.get16(40)},cw,status,s.fp.bytes[4]&(1u<<top),plan))return false;
    {fp::detail::Stack stack(&s.fp);stack.environment.status&=~fp::RoundUp;stack.pop();}
    s.fp.put64(8,0x474f12);s.fp.put16(6,1308&0x7ff);s.fp.put64(16,plan.operand);
    apply_text_ftol(s,scope,plan);
    return true;
}
}
