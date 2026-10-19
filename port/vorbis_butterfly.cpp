// Exact x87 operations over checked copies of the original 32-point butterfly.
#include "vorbis_butterfly.hpp"

extern "C" void wa2_vorbis_butterfly_native(void*,void*);
namespace wr {
namespace {
#include "vorbis_butterfly_signature.inc"
bool overlaps(std::uint64_t a,std::uint64_t n,std::uint64_t b,std::uint64_t k) {
    return a<b+k && b<a+n;
}
}
unsigned wa2_vorbis_butterfly(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc) {
    constexpr U32 begin=0x46e477,end=0x46e95c,constants=0x4a6e8c;
    if(s.eip!=begin || limit<butterfly_instructions || budget<butterfly_instructions ||
       (return_pc>=begin && return_pc<end) || (s.fp.get16(0)&0x3f)!=0x3f)return 0;
    const auto data=s.r[EAX],sp=s.r[ESP];
    if(data>0xffffff80u || sp<4 || sp>0xfffffff0u ||
       overlaps(data,128,sp-4,20) || overlaps(data,128,constants,12) ||
       overlaps(sp-4,20,constants,12) || overlaps(data,128,begin,end-begin) ||
       overlaps(sp-4,20,begin,end-begin))return 0;
    alignas(16) std::array<std::uint8_t,176> scratch{};
    auto guard=m.lock();
    try {
        if(!m.matches_code(begin,butterfly_code))return 0;
        m.check(data,128,Memory::Read|Memory::Write);
        m.check(sp-4,20,Memory::Read|Memory::Write);
        m.check(constants,12,Memory::Read);
        m.copy_out(data,std::span(scratch).first(128));
        m.copy_out(sp-4,std::span(scratch).subspan(128,20));
        m.copy_out(constants,std::span(scratch).subspan(148,12));
    }catch(const GuestFault&){return 0;}
    // PUSH ECX at 0x46e904 writes a guest address. No preceding instruction
    // accesses this slot; the kernel changes its scratch stack base there.
    for(unsigned k=0;k<4;++k)scratch[128+k]=std::uint8_t((data+96)>>(k*8));
    auto result=s.fp;
    wa2_vorbis_butterfly_native(result.bytes.data(),scratch.data());
    result.put16(0,s.fp.get16(0));
    // Final FSTP ST(0) retains the DP of the preceding FSTP [EAX+0x54].
    result.put64(8,0x46e95a);result.put64(16,data+0x54);result.put16(6,0x5d8);
    m.copy_in(data,std::span(scratch).first(128));
    m.copy_in(sp-4,std::span(scratch).subspan(128,20));
    U32 edx=0;for(unsigned k=0;k<4;++k)edx|=U32(scratch[160+k])<<(k*8);
    s.fp=result;s.r[ECX]=data+96;s.r[EDX]=edx;s.r[ESP]=sp-4;s.eip=end;
    budget-=butterfly_instructions;return butterfly_instructions;
}
}
