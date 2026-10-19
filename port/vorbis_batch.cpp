// Exact native x87 batching for the original Vorbis MDCT butterfly. All guest
// reads/writes use checked scratch copies; this does not expose native pointers.
#include "vorbis_batch.hpp"
#include <winrecomp/integer.hpp>

extern "C" void wa2_vorbis_mdct_native(void*,void*);
namespace wr {
namespace {
#include "vorbis_signature.inc"
bool overlaps(std::uint64_t a,std::uint64_t n,std::uint64_t b,std::uint64_t k) {
    return a<b+k && b<a+n;
}
}
unsigned wa2_vorbis_batch(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc) {
    constexpr U32 begin=0x46ea23,end=0x46eb14;
    if(s.eip!=begin || limit<mdct_instructions || budget<mdct_instructions ||
       (return_pc>=begin && return_pc<end) || (s.fp.get16(0)&0x3f)!=0x3f)return 0;
    const auto a=s.r[EAX],c=s.r[ECX],d=s.r[EDX],stride=s.r[ESI];
    // Guard subtraction as well as memory ranges; fall back on 32-bit wrap.
    if(a<32 || a>0xffffffe0u || c<32 || c>0xfffffffbu ||
       overlaps(a,32,c-28,32) || overlaps(a,32,begin,end-begin) ||
       overlaps(c-28,32,begin,end-begin))return 0;
    alignas(16) std::array<std::uint8_t,96> scratch{};
    const auto weights=std::uint64_t(d)+std::uint64_t(stride)*3;
    if(weights+8>0x100000000ull)return 0;
    // Keep preflight and commit under the same recursive lock. In normal guest
    // execution the caller already owns the outer execution scope.
    auto guard=m.lock();
    try {
        if(!m.matches_code(begin,mdct_code))return 0;
        m.check(a,32,Memory::Read|Memory::Write);
        m.check(c-28,32,Memory::Read|Memory::Write);
        for(unsigned k=0;k<4;++k) {
            const auto at=d+stride*k;
            if(overlaps(at,8,a,32) || overlaps(at,8,c-28,32))return 0;
            m.check(at,8,Memory::Read);
        }
        m.copy_out(a,std::span(scratch).first(32));
        m.copy_out(c-28,std::span(scratch).subspan(32,32));
        for(unsigned k=0;k<4;++k)m.copy_out(d+stride*k,std::span(scratch).subspan(64+k*8,8));
    }catch(const GuestFault&){return 0;}
    auto result=s.fp;
    wa2_vorbis_mdct_native(result.bytes.data(),scratch.data());
    result.put16(0,s.fp.get16(0));
    // The final instruction is FSTP [old EAX+4] at 0x46eb0b. Preserve the
    // runtime's guest-visible environment instead of leaking native addresses.
    result.put64(8,0x46eb0b);result.put64(16,a+4);result.put16(6,344);
    m.copy_in(a,std::span(scratch).first(32));
    m.copy_in(c-28,std::span(scratch).subspan(32,32));
    s.fp=result;s.r[EAX]=a-32;s.r[ECX]=c-32;s.r[EDX]=d+stride*4;
    sub(s,s.r[EAX],s.r[EDI],32);
    s.eip=s.flags&CF?end:begin;
    budget-=mdct_instructions;
    return mdct_instructions;
}
}
