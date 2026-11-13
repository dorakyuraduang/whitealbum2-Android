#include "text_spacing.hpp"
#include "fp_execution.hpp"
#include <bit>
namespace wa2::aot {namespace {
bool overlap(const void* a,unsigned na,const void* b,unsigned nb){
    const auto x=reinterpret_cast<std::uintptr_t>(a),y=reinterpret_cast<std::uintptr_t>(b);
    return x<=y?y-x<na:x-y<nb;
}
}
bool native_text_spacing(wr::Cpu& s,wr::Memory&,wr::Memory::ExecutionScope& scope){
    if(s.eip!=0x445350)return false;
    const auto sp=s.r[wr::ESP],input=s.r[wr::EAX];if(sp<8)return false;
    const bool ordinary=input!=0xfffffffeu && input!=0xffffffffu;
    const auto* ret=fp::detail::operand_page(scope,sp,4,wr::Memory::Read);
    const auto* save=fp::detail::operand_page(scope,sp-4,4,wr::Memory::Read|wr::Memory::Write);
    const auto* carry=fp::detail::operand_page(scope,0xb34544,4,wr::Memory::Read|wr::Memory::Write);
    const auto* second=ordinary?fp::detail::operand_page(scope,sp-8,4,wr::Memory::Read|wr::Memory::Write):nullptr;
    if(!ret || !save || !carry || (ordinary && !second) ||
       ((save->permissions|carry->permissions|(second?second->permissions:0))&wr::Memory::Execute))return false;
    const auto* rp=ret->data+(sp&4095);const auto* bp=save->data+((sp-4)&4095);const auto* cp=carry->data+(0xb34544&4095);
    const auto* dp=second?second->data+((sp-8)&4095):nullptr;
    if(overlap(rp,4,bp,4) || overlap(cp,4,rp,4) || overlap(cp,4,bp,4) ||
       (dp && (overlap(dp,4,rp,4) || overlap(dp,4,bp,4) || overlap(dp,4,cp,4))))return false;
    const auto returning=scope.load<32>(sp),previous=scope.load<32>(0xb34544);
    const auto saved_si=s.r[wr::ESI],saved_di=s.r[wr::EDI];
    scope.store<32>(sp-4,saved_si);
    if(input==0xfffffffeu){
        (void)wr::sub(s,input,0xfffffffeu,32);s.r[wr::EAX]=previous;
    }else if(input==0xffffffffu){
        (void)wr::sub(s,input,0xffffffffu,32);s.r[wr::EAX]=wr::logic(s,0,32);scope.store<32>(0xb34544,0);
    }else{
        // Both source magic-multiply sequences implement signed /10, truncated
        // toward zero. The intervening sum wraps as a source 32-bit register.
        const auto first=std::bit_cast<std::int32_t>(input)/10;
        const auto remainder=input-std::uint32_t(first)*10u;
        const auto combined=previous+remainder;
        const auto second_quotient=std::bit_cast<std::int32_t>(combined)/10;
        const auto q=std::uint32_t(second_quotient);
        scope.store<32>(sp-8,saved_di);
        s.r[wr::EAX]=std::uint32_t(first)+q;
        s.r[wr::ECX]=wr::sub(s,0u-q*4u,q,32);
        s.r[wr::EDX]=combined-q*10u;
        scope.store<32>(0xb34544,s.r[wr::EDX]);
    }
    s.r[wr::ESP]=sp+4;s.eip=returning;return true;
}
}
