#pragma once
#include "winrecomp/integer.hpp"
#include <chrono>
namespace wr {
// The supported source ISA is a build-time contract. Reporting the physical
// host CPU would select different source implementations on x64 and ARM64.
inline void guest_cpuid(Cpu& s){
    const auto leaf=s.r[EAX];s.r[EAX]=s.r[EBX]=s.r[ECX]=s.r[EDX]=0;
    switch(leaf){
    case 0:s.r[EAX]=1;s.r[EBX]=0x756e6547;s.r[EDX]=0x49656e69;s.r[ECX]=0x6c65746e;break;
    case 1:s.r[EAX]=0x000006f0;s.r[EBX]=0x00000800;s.r[EDX]=0x07008111;break;
    case 0x80000000:s.r[EAX]=0x80000001;break;
    case 0x80000001:break;
    default:break;
    }
}
using TimestampSource=std::uint64_t(*)();
inline thread_local TimestampSource timestamp_source{};
inline void guest_rdtsc(Cpu& s){
    const auto count=timestamp_source?timestamp_source():std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    s.r[EAX]=U32(count);s.r[EDX]=U32(count>>32);
}
inline void guest_popfd(Cpu& s,Memory& m) {
    const auto value=m.load(s.r[ESP],32);
    // CPL3/IOPL0 profile: IF/IOPL remain unchanged; TF/NT delivery is unsupported.
    if(value&(0x100u|0x4000u))throw GuestFault(FaultKind::unsupported,s.eip,"POPF requested TF/NT outside the flat guest profile");
    constexpr U32 writable=STATUS_FLAGS|DF|0x40000u|0x200000u;
    s.flags=((s.flags&~writable)|(value&writable)|2u);
    s.defined_flags|=STATUS_FLAGS; s.r[ESP]+=4;
}
}
