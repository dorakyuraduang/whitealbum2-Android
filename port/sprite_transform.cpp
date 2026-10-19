#include "sprite_transform.hpp"
extern "C" void wa2_sprite_transform_native(void*,void*,void*);
namespace wr {
namespace {
#include "sprite_transform_signature.inc"
bool overlap(std::uint64_t a,std::uint64_t n,std::uint64_t b,std::uint64_t k){return a<b+k && b<a+n;}
constexpr std::array<U32,15> written={0x10,0x14,0x1c,0x28,0x2c,0x30,0x48,0x80,0x84,0x88,0x8c,0x90,0x94,0x98,0x9c};
}
unsigned wa2_sprite_transform_batch(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc){
    constexpr U32 begin=0x43bba8,end=0x43bde0;
    if(s.eip!=begin || budget<sprite_transform_minimum || limit<sprite_transform_minimum ||
       (return_pc>=begin && return_pc<end) || (s.fp.get16(0)&0x3f)!=0x3f || (s.fp.get32(24)&0xffff0000u))return 0;
    const U32 sp=s.r[ESP],sprite=s.r[ESI];
    if(sp>0xffffff60u || sprite>0xffffff38u || overlap(sp,0xa0,sprite,0xc8) || overlap(sp,0xa0,begin,end-begin))return 0;
    // Assembly accesses only these local snapshots: stack, sprite, then the
    // indexed palette byte. Reject aliases whose sequential reads could change.
    alignas(16) std::array<std::uint8_t,0x169> scratch{};
    U32 index{};auto guard=m.lock();
    try{
        if(!m.matches_code(begin,sprite_transform_code))return 0;
        m.copy_out(sp,std::span(scratch).first(0xa0));m.copy_out(sprite,std::span(scratch).subspan(0xa0,0xc8));
        index=sign_extend(scratch[0xa0+0x52],8);const U32 palette=0x4c2293u+index*4;
        if(overlap(sp,0xa0,palette,1))return 0;
        for(auto offset:written)m.check(sp+offset,4,Memory::Write);
        scratch[0x168]=std::uint8_t(m.load(palette,8));
    }catch(const GuestFault&){return 0;}
    std::array<U32,9> context{};std::copy(s.r.begin(),s.r.end(),context.begin());auto fp=s.fp;
    wa2_sprite_transform_native(fp.bytes.data(),scratch.data(),context.data());
    const auto count=context[8];if(count>budget || count>limit)return 0;
    fp.put16(0,s.fp.get16(0)); // x87_execute preserves the original reserved CW bits.
    fp.put64(8,0x43bdd0);fp.put64(16,sp+0x9c);fp.put16(6,412);
    for(auto offset:written)m.copy_in(sp+offset,std::span(scratch).subspan(offset,4));
    // XOR's AF is undefined but the interpreter retains the preceding ADD's
    // value. Reproduce that retained bit rather than taking native XOR flags.
    add(s,index*2,index*2,32);logic(s,0,32);
    std::copy_n(context.begin(),8,s.r.begin());s.fp=fp;s.eip=end;budget-=count;return count;
}
}
