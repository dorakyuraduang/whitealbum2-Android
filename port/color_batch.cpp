#include "color_batch.hpp"
#include <bit>
namespace wr {
namespace {
#include "color_signature.inc"
bool overlap(std::uint64_t a,std::uint64_t n,std::uint64_t b,std::uint64_t k){return a<b+k && b<a+n;}
std::int32_t signed_value(U32 n){return std::bit_cast<std::int32_t>(n);}
}
unsigned wa2_color_batch(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc){
    constexpr U32 begin=0x43b290,end=0x43b3bf;
    const auto sp=s.r[ESP];
    if(s.eip!=begin || limit<50 || budget<50 || (return_pc>=begin && return_pc<end) || sp<20 || sp>0xffffffe3u || overlap(sp-20,48,begin,end-begin))return 0;
    U32 index{},element{},alpha{},red{},green{},blue{},return_address{},flag{},first{},second{};
    try{
        if(!m.matches_code(begin,color_code))return 0;
        m.check(sp-20,48,Memory::Read|Memory::Write);
        return_address=m.load(sp,32);index=m.load(sp+4,32);element=m.load(sp+8,32);
        alpha=m.load(sp+12,32);red=m.load(sp+16,32);green=m.load(sp+20,32);blue=m.load(sp+24,32);
        const auto flag_address=(index<<8)+0xab4002u;
        if(overlap(flag_address,2,sp-20,48))return 0;flag=m.load(flag_address,16);
        const auto offset=((index<<6)+element)*4;first=offset+0xab400bu;second=offset+0xab401bu;
        if(overlap(first,4,sp-20,48) || overlap(second,4,sp-20,48) || overlap(first,4,begin,end-begin) || overlap(second,4,begin,end-begin))return 0;
        m.check(first,4,Memory::Write);m.check(second,4,Memory::Write);
    }catch(const GuestFault&){return 0;}
    unsigned instructions=13;U32 add_red=0,add_green=0,add_blue=0;
    if(flag&0x8000u){++instructions;}
    else{
        if(signed_value(red)>=128){add_red=red*2-256;instructions+=8;if(signed_value(add_red)>=255){add_red=255;++instructions;}red=255;}
        else{red*=2;if(signed_value(red)<0)red=0;instructions+=10;}
        if(signed_value(green)>=128){add_green=green*2-256;instructions+=9;if(signed_value(add_green)>=255){add_green=255;++instructions;}green=255;}
        else{green*=2;if(signed_value(green)<0)green=0;instructions+=10;}
        if(signed_value(blue)>=128){add_blue=blue*2-256;instructions+=9;if(signed_value(add_blue)>=255){add_blue=255;++instructions;}blue=255;}
        else{blue*=2;if(signed_value(blue)<0)blue=0;instructions+=10;}
    }
    if(signed_value(alpha)<=0)alpha=0;
    instructions+=36;if(signed_value(alpha)>=255){alpha=255;++instructions;}
    if(instructions>limit || instructions>budget)return 0;
    // Preserve the function's stack scratch bytes and saved-register writes,
    // including the two in-place argument updates, for exact guest equivalence.
    m.store(sp-12,s.r[EBX],32);m.store(sp-16,s.r[EBP],32);m.store(sp-20,s.r[ESI],32);
    m.store(sp-8,add_green,32);m.store(sp-4,add_blue,32);
    if(!(flag&0x8000u)){m.store(sp+16,red,32);m.store(sp+20,green,32);}
    const auto additive=0xff000000u|((add_red&255u)<<16)|((add_green&255u)<<8)|(add_blue&255u);
    const auto modulation=(alpha<<24)|((red&255u)<<16)|((green&255u)<<8)|(blue&255u);
    m.store(first,additive,32);m.store(second,modulation,32);
    s.r[EAX]=modulation;s.r[ECX]=green&255u;s.r[EDX]=blue&255u;
    add(s,sp-8,8,32);s.r[ESP]=sp+4;s.eip=return_address;
    budget-=instructions;return instructions;
}
}
