#include "vertex_batch.hpp"
#include "native_backend.hpp"
namespace wr {
namespace {
#include "vertex_signature.inc"
bool overlap(std::uint64_t a,std::uint64_t n,std::uint64_t b,std::uint64_t k){return a<b+k && b<a+n;}
U32 word(std::span<const std::uint8_t> bytes,unsigned at){return U32(bytes[at])|(U32(bytes[at+1])<<8)|(U32(bytes[at+2])<<16)|(U32(bytes[at+3])<<24);}
void put(std::span<std::uint8_t> bytes,unsigned at,U32 value){for(unsigned i=0;i<4;++i)bytes[at+i]=std::uint8_t(value>>(i*8));}
unsigned clamped(int value){return unsigned(std::clamp(value,0,510));}
int combine(unsigned sum,unsigned palette,bool additive){return additive?int(sum)+int(palette)*4-512+(palette>128?2:0):palette<=128?int(sum*palette/128):int(sum+(palette-128)*(510-sum)/127);}
}
template<bool Metered>
unsigned vertex_batch(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc){
    constexpr U32 begin=0x43bde0,end=0x43c100;
    const auto sp=s.r[ESP],vertex=s.r[EDI];
    if(s.eip!=begin || (Metered && (limit<100 || budget<100)) || vertex>12 || (vertex&3) || sp>0xffffff83u || (return_pc>=begin && return_pc<end) || overlap(sp,0x7c,begin,end-begin))return 0;
    std::array<std::uint8_t,0x7c> stack;std::array<std::uint8_t,32> source{};std::array<std::uint8_t,3> initial_palette{},palette{};
    U32 source_address{},palette_offset{},mode{};const auto iterations=(16-vertex)/4;
    try{
        if(!wa2_native_code_verified(m,begin) && !m.matches_code(begin,vertex_code))return 0;
        m.check(sp,stack.size(),Memory::Read|Memory::Write);m.copy_out(sp,stack);
        mode=word(stack,0x10);if(mode>1 || stack[0x14]>1)return 0;
        source_address=word(stack,0x48);palette_offset=word(stack,0x2c);
        const auto source_bytes=16+iterations*4,first_palette=s.r[EDX]+0x4c2290u,next_palette=palette_offset+0x4c2290u;
        if(overlap(source_address,source_bytes,sp,stack.size()) || overlap(first_palette,3,sp,stack.size()) || overlap(next_palette,3,sp,stack.size()))return 0;
        m.copy_out(source_address,std::span(source).first(source_bytes));m.copy_out(first_palette,initial_palette);m.copy_out(next_palette,palette);
    }catch(const GuestFault&){return 0;}
    unsigned executed=0,done=0;U32 eax{},ebx{},ecx{},esi=s.r[ESI];
    for(unsigned i=0;i<iterations;++i){
        const auto vi=vertex+i*4;const auto& current=i?palette:initial_palette;
        const unsigned pr=current[0],pg=mode?current[1]:palette[1],pb=mode?current[2]:palette[2];
        const auto sr=unsigned(source[i*4+18])+source[i*4+2],sg=unsigned(source[i*4+17])+source[i*4+1],sb=unsigned(source[i*4+16])+source[i*4];
        const auto raw_r=combine(sr,pr,mode!=0),raw_g=combine(sg,pg,mode!=0),raw_b=combine(sb,pb,mode!=0);
        const auto r=clamped(raw_r),g=clamped(raw_g),b=clamped(raw_b);
        unsigned instructions=mode?11+(pr<=128?10:9)+(pg<=128?9:8)+9:9+(pr<=128?14:21)+(pg<=128?15:20)+(pb<=128?16:22);
        instructions+=(stack[0x14]?5:3)+8+(raw_r>=510)+9+(raw_g>=510)+8+(raw_b>=510);
        instructions+=(r>=255?9+(r==510):10)+(g>=255?9+(g==510):10)+(b>=255?6+(b==510?2:0):9)+14+4;
        if constexpr(Metered)if(instructions>limit-executed || instructions>budget-executed)break;
        const auto mr=std::min(r,255u),mg=std::min(g,255u),mb=std::min(b,255u),ar=r-mr,ag=g-mg,ab=b-mb;
        // Source colors, the five scratch words and every output byte match the
        // original stores. The private buffer makes the commit atomic only after
        // proving that the whole iteration cannot fault or alias another input.
        put(stack,0x38+vi,word(source,16+i*4));put(stack,0x50+vi,word(source,i*4));
        put(stack,0x28,mr);put(stack,0x4c,g);put(stack,0x78,b);put(stack,0x30,ar);put(stack,0x1c,ag);
        stack[0x3a+vi]=std::uint8_t(mr);stack[0x3b+vi]=source[i*4+19];stack[0x52+vi]=std::uint8_t(ar);stack[0x51+vi]=std::uint8_t(ag);
        stack[0x39+vi]=std::uint8_t(mg);stack[0x38+vi]=std::uint8_t(mb);stack[0x50+vi]=std::uint8_t(ab);stack[0x53+vi]=255;
        put(stack,0x48,source_address+(i+1)*4);eax=ab;ebx=mg;ecx=mb;
        if(!mode && (pr>128 || pb>128))esi=word(stack,0x64);
        if constexpr(Metered)executed+=instructions;else ++executed;
        ++done;
    }
    if(!done)return 0;
    m.copy_in(sp,stack);s.r[EAX]=eax;s.r[EBX]=ebx;s.r[ECX]=ecx;s.r[EDX]=palette_offset;s.r[ESI]=esi;s.r[EDI]+=done*4;
    sub(s,s.r[EDI],16,32);s.eip=s.r[EDI]<16?begin:end;if constexpr(Metered)budget-=executed;return executed;
}
#if !defined(WA2_AOT_DIAGNOSTICS) || WA2_AOT_DIAGNOSTICS
unsigned wa2_vertex_batch(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc){return vertex_batch<true>(s,m,budget,limit,return_pc);}
#endif
bool wa2_vertex_full(Cpu& s,Memory& m){std::uint64_t unused{};return vertex_batch<false>(s,m,unused,0,0)!=0;}
}
