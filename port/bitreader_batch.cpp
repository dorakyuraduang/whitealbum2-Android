#include "bitreader_batch.hpp"
#include <winrecomp/integer.hpp>

namespace wr {
namespace {
#include "bitreader_signature.inc"
bool overlap(std::uint64_t a,std::uint64_t n,std::uint64_t b,std::uint64_t k) {
    return a<b+k && b<a+n;
}
bool less(const Cpu& s) { return bool(s.flags&SF)!=bool(s.flags&OF); }
bool less_equal(const Cpu& s) { return (s.flags&ZF)||less(s); }
}
unsigned wa2_bitreader_batch(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc) {
    constexpr U32 begin=0x49fb20,end=0x49fbc7;
    if(s.eip!=begin || budget<24 || limit<24 || (return_pc>=begin && return_pc<end))return 0;
    const auto sp=s.r[ESP];
    if(sp<16 || sp>0xfffffff3u || overlap(sp-16,16,begin,end-begin))return 0;
    auto guard=m.lock();
    U32 context{},requested{},mask_value{},consumed{},offset{},total{},source{},destination{};
    std::array<std::uint8_t,5> input{};
    // Arithmetic stays local until every checked read/write and the whole
    // dynamic instruction path fit. Failure leaves the exact scalar fault PC.
    Cpu arithmetic;arithmetic.flags=s.flags;arithmetic.defined_flags=s.defined_flags;
    U32 eax{},ecx{},edx{};unsigned instructions=14;
    bool exhausted=false;
    try {
        if(!m.matches_code(begin,bitreader_code))return 0;
        m.check(sp,12,Memory::Read);m.check(sp-16,16,Memory::Read|Memory::Write);
        destination=m.load(sp,32);context=m.load(sp+4,32);requested=m.load(sp+8,32);
        if(requested>32 || context>0xffffffebu || overlap(sp-16,16,context,20))return 0;
        const U32 mask_address=0x4d2c50+requested*4;
        if(overlap(sp-16,16,mask_address,4))return 0;
        m.check(context,20,Memory::Read);
        consumed=m.load(context,32);offset=m.load(context+4,32);total=m.load(context+16,32);
        if(offset>7)return 0;
        mask_value=m.load(mask_address,32);
        ecx=context;edx=add(arithmetic,requested,offset,32);
        sub(arithmetic,consumed+4,total,32);
        if(!less(arithmetic)) {
            eax=edx+consumed*8;
            const auto shifted_total=shift(arithmetic,total,3,32,0);
            sub(arithmetic,eax,shifted_total,32);
            instructions+=4;exhausted=!less_equal(arithmetic);
        }
        if(exhausted) {
            eax=logic(arithmetic,0xffffffffu,32);instructions+=6;
        }else {
            source=m.load(context+12,32);
            const auto bytes=std::max(1u,(edx+7)/8);
            if(std::uint64_t(source)+bytes>0x100000000ull || overlap(sp-16,16,source,bytes))return 0;
            m.copy_out(source,std::span(input).first(bytes));
            ecx=(context&0xffffff00u)|offset;
            eax=logic(arithmetic,0,32);eax=input[0];
            eax=shift(arithmetic,eax,ecx&255,32,1);
            sub(arithmetic,edx,8,32);instructions+=7;
            // These are the three original, statically unrolled byte stages.
            // The same helper operations preserve AF and undefined flag bits.
            for(unsigned stage=1;stage<=3 && edx>stage*8;++stage) {
                ecx=logic(arithmetic,0,32);ecx=input[stage];
                U32 ebx=ecx;ecx=stage*8;
                ecx=sub(arithmetic,ecx,offset,32);
                ebx=shift(arithmetic,ebx,ecx&255,32,0);
                eax=logic(arithmetic,eax|ebx,32);
                sub(arithmetic,edx,(stage+1)*8,32);instructions+=9;
            }
            if(edx>32) {
                logic(arithmetic,offset,32);instructions+=2;
                if(offset) {
                    edx=logic(arithmetic,0,32);ecx=32;edx=input[4];
                    ecx=sub(arithmetic,ecx,offset,32);
                    edx=shift(arithmetic,edx,ecx&255,32,0);
                    eax=logic(arithmetic,eax|edx,32);instructions+=6;
                }
            }
            eax=logic(arithmetic,eax&mask_value,32);instructions+=6;
        }
    }catch(const GuestFault&){return 0;}
    if(budget<instructions || limit<instructions)return 0;
    // POP restores all four saved registers, but the original PUSH writes are
    // observable below ESP and must still occur on both success and EOF paths.
    m.store(sp-4,s.r[EBX],32);m.store(sp-8,s.r[EBP],32);
    m.store(sp-12,s.r[ESI],32);m.store(sp-16,s.r[EDI],32);
    s.r[EAX]=eax;s.r[ECX]=ecx;s.r[EDX]=edx;s.r[ESP]=sp+4;
    s.flags=arithmetic.flags;s.defined_flags=arithmetic.defined_flags;s.eip=destination;
    budget-=instructions;return instructions;
}
}
