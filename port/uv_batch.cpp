#include "uv_batch.hpp"
#include <bit>
extern "C" void wa2_uv_x_native(void*,void*,void*);
extern "C" void wa2_uv_y_native(void*,void*,void*);
namespace wr {
namespace {
#include "uv_signature.inc"
bool overlap(std::uint64_t a,std::uint64_t n,std::uint64_t b,std::uint64_t k){return a<b+k && b<a+n;}
}
unsigned wa2_uv_batch(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc){
    const bool y=s.eip==0x43cd37;const U32 begin=y?0x43cd37:0x43cc4d,end=y?0x43cdf0:0x43cd06;
    if(s.eip!=begin || limit<30 || budget<30 || (return_pc>=begin && return_pc<end) ||
       (y && return_pc>=0x43d77d && return_pc<0x43d78c) || (s.fp.get16(0)&0x3f)!=0x3f || (s.fp.get32(24)&0xffff0000u))return 0;
    const auto sp=s.r[ESP],index=s.r[ECX];
    const std::uint64_t a64=std::uint64_t(sp)+std::uint64_t(index)*4+(y?0x340:0x268),b64=std::uint64_t(sp)+std::uint64_t(index)*4+(y?0x2b0:0x2f8);
    if(sp>0xffffff8fu || a64+4>0x100000000ull || b64+4>0x100000000ull)return 0;
    const auto a=U32(a64),b=U32(b64);
    const auto code_overlap=[&](U32 at){return overlap(at,4,begin,end-begin) || (y && overlap(at,4,0x43d77d,15));};
    for(auto at:{sp+0x10,sp+0x1c,sp+0x30,a,b})if(code_overlap(at))return 0;
    // Only checked guest words enter the assembly. Its 28-byte scratch layout
    // fixes both array elements, so no guest address is a native pointer.
    alignas(16) std::array<std::uint8_t,28> scratch{};
    auto guard=m.lock();
    try{
        if(!(y?m.matches_code(begin,uv_y_0_code)&&m.matches_code(0x43d77d,uv_y_1_code):m.matches_code(begin,uv_x_0_code)))return 0;
        m.check(sp+0x10,4,Memory::Read|Memory::Write);m.check(sp+0x1c,4,Memory::Read|Memory::Write);m.check(sp+0x30,4,Memory::Read|Memory::Write);
        m.check(a,4,Memory::Write);m.check(b,4,Memory::Write);m.copy_out(sp+0x30,std::span(scratch).first(4));
        if(y){m.copy_out(sp+0x68,std::span(scratch).subspan(12,4));m.copy_out(sp+0x6c,std::span(scratch).subspan(16,4));}
    }catch(const GuestFault&){return 0;}
    std::array<U32,3> context={s.r[EAX],s.r[EDX],0};auto result=s.fp;
    (y?wa2_uv_y_native:wa2_uv_x_native)(result.bytes.data(),scratch.data(),context.data());
    const auto instructions=context[2];if(instructions>limit || instructions>budget)return 0;
    result.put16(0,s.fp.get16(0)); // Match x87_execute's exact guest CW restoration.
    result.put64(8,y?0x43cde0:0x43ccf6);result.put64(16,b);result.put16(6,412);
    m.copy_in(sp+0x30,std::span(scratch).first(4));m.copy_in(sp+0x10,std::span(scratch).subspan(4,4));m.copy_in(sp+0x1c,std::span(scratch).subspan(8,4));
    m.copy_in(a,std::span(scratch).subspan(20,4));m.copy_in(b,std::span(scratch).subspan(24,4));
    s.fp=result;s.r[EAX]=context[0];++s.r[ECX];sub(s,s.r[ECX],s.r[y?ESI:EBX],32);
    s.eip=(bool(s.flags&SF)!=bool(s.flags&OF))?begin:end;budget-=instructions;return instructions;
}
}
