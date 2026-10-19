// SHA-bound static translations of measured hot loops. This is
// compiled with the game; it neither decodes nor executes x86 at runtime.
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string_view>
#include <winrecomp/integer.hpp>
#include <winrecomp/sse.hpp>
#include "fast_paths.hpp"
#include "legacy_vorbis_kernels.hpp"
#include "vertex_batch.hpp"
#include "uv_batch.hpp"
#include "fp_sequences.hpp"
#include "sprite_transform.hpp"
#include "bitreader_batch.hpp"
#include "color_batch.hpp"
#include "native_backend.hpp"
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
extern "C" void wr_sse_102_127_m0(void*,void*,std::uint32_t*);
namespace wr {
namespace {
#include "lzss_signature.inc"
thread_local std::uint64_t lzss_linear_batches{},lzss_linear_bytes{};
thread_local std::uint64_t pak_linear_batches{},pak_linear_bytes{};
bool ranges_overlap(std::uint64_t a,std::uint64_t n,std::uint64_t b,std::uint64_t k){return a<b+k && b<a+n;}
bool linear_range(const Memory::ExecutionScope& scope,U32 at,std::uint64_t size,unsigned access){
    auto* base=scope.linear_base();if(!base || std::uint64_t(at)+size>0x100000000ull)return false;
    for(std::uint64_t cursor=at,end=std::uint64_t(at)+size;cursor<end;){
        const auto* table=scope.native_pages()[cursor>>22];if(!table)return false;
        const auto& page=(*table)[(cursor>>12)&1023];const auto offset=unsigned(cursor&4095);
        const auto amount=unsigned(std::min<std::uint64_t>(end-cursor,4096-offset));
        if((page.permissions&access)!=access || ((access&Memory::Write) && (page.permissions&Memory::Execute)) ||
           offset+amount>page.length || page.data!=base+(cursor&~4095ull))return false;
        cursor+=amount;
    }
    return true;
}
unsigned pak_linear_tokens(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc){
    constexpr U32 begin=0x457090,end=0x4571bc;
    if(limit<19 || budget<19 || (return_pc>=begin && return_pc<end) || s.r[ESP]>0xffffefb0u)return 0;
    auto scope=m.execution_scope();auto* base=scope.linear_base();if(!base)return 0;
    const auto sp=s.r[ESP],ring=sp+0x24;
    if(!linear_range(scope,sp+0x10,0x1034,Memory::Read|Memory::Write) || !wa2_native_code_verified(m,begin))return 0;
    const auto get32=[&](U32 at){U32 value;std::memcpy(&value,base+at,4);return value;};
    const auto put32=[&](U32 at,U32 value){std::memcpy(base+at,&value,4);};
    auto remaining=std::bit_cast<std::int32_t>(get32(sp+0x1040));const auto maximum=get32(sp+0x14),written=s.r[EDI];
    if(remaining<=0 || std::int32_t(written)<0 || written>=maximum || maximum>0x7fffffffu)return 0;
    const auto available=unsigned(std::min<std::uint64_t>(limit,budget));
    const auto input=std::min<std::uint64_t>(unsigned(remaining),(std::uint64_t(available)/19+1)*3);
    const auto output=std::min<std::uint64_t>(maximum-written,std::uint64_t(available)/14+18);
    const auto source_start=s.r[EBX],destination_start=s.r[ESI];
    if(!linear_range(scope,source_start,input,Memory::Read) || !linear_range(scope,destination_start,output,Memory::Write) ||
       ranges_overlap(source_start,input,sp,0x1048) || ranges_overlap(destination_start,output,sp,0x1048) ||
       ranges_overlap(destination_start,output,source_start,unsigned(remaining)))return 0;
    unsigned executed=0;
    while(available-executed>=19 && remaining>0 && s.r[EDI]<maximum){
        auto source=s.r[EBX],flags=s.r[ECX]>>1;int left=remaining;const bool new_flags=!(flags&0x100);
        if(new_flags){flags=base[source++]+0xff00u;--left;}
        if(left<=0)break;U32 value=base[source++];--left;const bool literal=flags&1;U32 size=1;
        if(!literal){if(left<=0)break;const auto high=base[source++];--left;value|=(high&0xf0)<<4;size=(high&15)+3;}
        const auto cost=(literal?19u:29u+14u*size)+(new_flags?7u:0u);
        if(cost>available-executed || std::uint64_t(s.r[EDI])+size>maximum)break;
        const auto destination=s.r[ESI];auto position=s.r[EDX];
        if(position>4095)return executed;
        if(literal){base[destination]=std::uint8_t(value);base[ring+position]=std::uint8_t(value);s.r[EAX]=value;
            sub(s,s.r[EDI]+1,maximum,32);position=incdec(s,position,32,false);s.r[EDX]=logic(s,position&4095u,32);
        }else{
            for(U32 n=0;n<size;++n){const auto byte=base[ring+((value+n)&4095u)];base[destination+n]=byte;base[ring+position]=byte;position=(position+1)&4095u;}
            s.r[EAX]=size;s.r[EBP]=value;s.r[EDX]=position;put32(sp+0x1c,source);sub(s,size,size-1,32);
        }
        s.r[EBX]=source;s.r[ECX]=flags;s.r[ESI]+=size;s.r[EDI]+=size;
        remaining=left;put32(sp+0x1040,U32(left));put32(sp+0x18,flags);
        s.eip=begin;budget-=cost;executed+=cost;
    }
    if(executed){++pak_linear_batches;pak_linear_bytes+=s.r[ESI]-destination_start;}return executed;
}
unsigned lzss_linear_tokens(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc){
    constexpr U32 ring=0xa7cdd0,begin=0x437040,end=0x43710a;
    if(limit<23 || budget<23 || (return_pc>=begin && return_pc<end) || s.r[ESP]>0xffffffc0u)return 0;
    auto scope=m.execution_scope();auto* base=scope.linear_base();if(!base)return 0;
    const auto sp=s.r[ESP];
    if(!linear_range(scope,sp+0x10,32,Memory::Read|Memory::Write) || !linear_range(scope,ring,4096,Memory::Read|Memory::Write))return 0;
    if(!m.matches_code(begin,lzss_code) || ranges_overlap(sp,48,ring,4096))return 0;
    const auto get32=[&](U32 at){U32 value;std::memcpy(&value,base+at,4);return value;};
    const auto put32=[&](U32 at,U32 value){std::memcpy(base+at,&value,4);};
    const auto source_size=std::bit_cast<std::int32_t>(get32(sp+0x28));
    const auto maximum=std::bit_cast<std::int32_t>(get32(sp+0x2c));
    const auto written=std::bit_cast<std::int32_t>(s.r[EBP]);auto consumed=get32(sp+0x14),flags=get32(sp+0x10);
    if(source_size<0 || consumed>=U32(source_size) || written<0 || written>=maximum)return 0;
    const auto available=unsigned(std::min<std::uint64_t>(limit,budget));
    // Bound each span by this scheduler slice, rather than rescanning the
    // remainder of a multi-megabyte compressed file for every small batch.
    const auto input=std::min<std::uint64_t>(U32(source_size)-consumed,(std::uint64_t(available)/23+1)*3);
    const auto output=std::min<std::uint64_t>(U32(maximum-written),std::uint64_t(available)/17+18);
    const auto source_start=s.r[EBX],destination_start=s.r[EDI];
    if(!linear_range(scope,source_start,input,Memory::Read) || !linear_range(scope,destination_start,output,Memory::Write) ||
       ranges_overlap(source_start,input,ring,4096) || ranges_overlap(source_start,input,sp,48) ||
       ranges_overlap(destination_start,output,ring,4096) || ranges_overlap(destination_start,output,sp,48) ||
       ranges_overlap(destination_start,output,source_start,U32(source_size)-consumed))return 0;
    unsigned executed=0;
    while(available-executed>=23){
        auto source=s.r[EBX],next_consumed=consumed,next_flags=flags;
        if(s.r[EBP]>=U32(maximum) || consumed>=U32(source_size))break;
        const bool new_flags=!(next_flags&0x100);
        if(new_flags){next_flags=base[source++]|0xff00u;++next_consumed;}
        if(next_consumed>=U32(source_size))break;
        U32 eax=base[source++];++next_consumed;const bool literal=next_flags&1;U32 size=1;
        if(!literal){if(next_consumed>=U32(source_size))break;const auto high=base[source++];++next_consumed;eax|=(high&0xf0)<<4;size=(high&15)+3;}
        const auto instructions=(literal?23u:32u+17u*size)+(new_flags?7u:0u);
        if(instructions>available-executed || std::uint64_t(s.r[EBP])+size>U32(maximum))break;
        U32 ecx{};const auto destination=s.r[EDI];
        if(literal){ecx=s.r[ESI]&4095u;base[ring+ecx]=std::uint8_t(eax);base[destination]=std::uint8_t(eax);}
        else{for(U32 n=0;n<size;++n){const auto index=(eax+n)&4095u,value=U32(base[ring+index]);ecx=(index&0xf00u)|value;base[ring+((s.r[ESI]+n)&4095u)]=std::uint8_t(value);base[destination+n]=std::uint8_t(value);}eax+=size;put32(sp+0x20,source);}
        consumed=next_consumed;put32(sp+0x14,consumed);
        s.r[EAX]=eax;s.r[ECX]=ecx;s.r[EDX]=literal?consumed:0xffffffffu;s.r[EBX]=source;
        s.r[EBP]+=size;s.r[ESI]+=size;s.r[EDI]+=size;
        sub(s,s.r[EBP],U32(maximum),32);flags=shift(s,next_flags,1,32,1);put32(sp+0x10,flags);
        s.eip=begin;budget-=instructions;executed+=instructions;
    }
    if(executed){++lzss_linear_batches;lzss_linear_bytes+=s.r[EDI]-destination_start;}return executed;
}
unsigned sse_fill(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc){
    constexpr U32 begin=0x47e798,end=0x47e7c8;
    static constexpr std::uint8_t code[]={102,15,127,7,102,15,127,71,16,102,15,127,71,32,102,15,127,71,48,102,15,127,71,64,102,15,127,71,80,102,15,127,71,96,102,15,127,71,112,141,191,128,0,0,0,73,117,208};
    if(limit<11 || budget<11 || (return_pc>=begin && return_pc<=end) || (s.r[EDI]&15) || (s.fp.get32(24)&0xffff0000u))return 0;
    // An entry with ECX=0 is a do/while loop, wrapping to 0xffffffff.
    const auto count=U32(std::min<std::uint64_t>({limit/11,budget/11,128,s.r[ECX]?std::uint64_t(s.r[ECX]):0x100000000ull}));
    const auto bytes=count*128,destination=s.r[EDI];
    try{if(!m.matches_code(begin,code) || ranges_overlap(destination,bytes,begin,end-begin))return 0;m.check(destination,bytes,Memory::Write);}
    catch(const GuestFault&){return 0;}
    // Retain the existing helper's precise FXSAVE state normalization. MOVDQA
    // changes no FP arithmetic state, so repeating it has the same final state.
    alignas(16) std::array<std::uint8_t,16> pattern{};
    sse_execute(s,wr_sse_102_127_m0,pattern.data());
    std::array<std::uint8_t,16384> output;
    for(U32 at=0;at<bytes;at+=16)std::copy(pattern.begin(),pattern.end(),output.begin()+at);
    m.copy_in(destination,std::span(output).first(bytes));
    s.r[EDI]+=bytes;s.r[ECX]=incdec(s,s.r[ECX]-count+1,32,true);
    s.eip=s.r[ECX]?begin:end;budget-=std::uint64_t(count)*11;return count*11;
}
unsigned lzss_tokens(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc){
    constexpr U32 ring=0xa7cdd0,begin=0x437040,end=0x43710a;
    if(limit<23 || budget<23 || (return_pc>=begin && return_pc<end) || s.r[ESP]>0xffffffc0u)return 0;
    try{if(const auto count=lzss_linear_tokens(s,m,budget,limit,return_pc))return count;}catch(const GuestFault&){}
    const auto sp=s.r[ESP];unsigned executed=0;
    auto overlaps=[](std::uint64_t a,std::uint64_t n,std::uint64_t b,std::uint64_t k){return a<b+k && b<a+n;};
    try{
        if(!m.matches_code(begin,lzss_code) || overlaps(sp,48,ring,4096))return 0;
        m.check(sp+0x10,32,Memory::Read|Memory::Write);m.check(ring,4096,Memory::Read|Memory::Write);
        const auto source_size=std::bit_cast<std::int32_t>(m.load(sp+0x28,32));
        const auto maximum=std::bit_cast<std::int32_t>(m.load(sp+0x2c,32));
        while(limit-executed>=23 && budget>=23){
            U32 flags=m.load(sp+0x10,32),consumed=m.load(sp+0x14,32),source=s.r[EBX];
            const auto written=std::bit_cast<std::int32_t>(s.r[EBP]);
            if(source_size<0 || consumed>U32(source_size) || written<0 || written>=maximum)break;
            const auto available=U32(source_size)-consumed;
            if(!available || std::uint64_t(source)+available>0x100000000ull || overlaps(source,available,ring,4096) || overlaps(source,available,sp,48))break;
            const bool new_flags=!(flags&0x100);
            if(new_flags){flags=m.load(source,8)|0xff00u;++source;++consumed;}
            if(consumed>=U32(source_size))break;
            U32 eax=m.load(source,8);++source;++consumed;
            const bool literal=flags&1;U32 size=1;
            if(!literal){if(consumed>=U32(source_size))break;const auto high=m.load(source,8);++source;++consumed;eax|=(high&0xf0)<<4;size=(high&15)+3;}
            const auto instructions=(literal?23u:32u+17u*size)+(new_flags?7u:0u);
            if(instructions>limit-executed || instructions>budget || std::int64_t(written)+size>maximum)break;
            const auto destination=s.r[EDI];
            if(overlaps(destination,size,ring,4096) || overlaps(destination,size,sp,48) || overlaps(destination,size,begin,end-begin) || overlaps(destination,size,s.r[EBX],available))break;
            m.check(destination,size,Memory::Write);
            U32 ecx{};
            if(literal){ecx=s.r[ESI]&4095u;m.store(ring+ecx,eax,8);m.store(destination,eax,8);}
            else{
                for(U32 n=0;n<size;++n){const auto index=(eax+n)&4095u;const auto value=m.load(ring+index,8);ecx=(index&0xf00u)|value;m.store(ring+((s.r[ESI]+n)&4095u),value,8);m.store(destination+n,value,8);}
                eax+=size;m.store(sp+0x20,source,32);
            }
            m.store(sp+0x14,consumed,32);
            s.r[EAX]=eax;s.r[ECX]=ecx;s.r[EDX]=literal?consumed:0xffffffffu;s.r[EBX]=source;
            s.r[EBP]+=size;s.r[ESI]+=size;s.r[EDI]+=size;
            sub(s,s.r[EBP],U32(maximum),32);const auto next_flags=shift(s,flags,1,32,1);m.store(sp+0x10,next_flags,32);
            s.eip=begin;budget-=instructions;executed+=instructions;
        }
    }catch(const GuestFault&){return executed;}
    return executed;
}
}
unsigned wa2_static_batch(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc){
    if(s.eip==0x457090)try{return pak_linear_tokens(s,m,budget,limit,return_pc);}catch(const GuestFault&){return 0;}
    if(s.eip==0x442290)return wa2_sort_objects(s,m,budget,limit,return_pc);
    if(s.eip==0x49fb20)return wa2_bitreader_batch(s,m,budget,limit,return_pc);
    if(s.eip==0x43bba8)return wa2_sprite_transform_batch(s,m,budget,limit,return_pc);
    if(s.eip==0x43cc4d || s.eip==0x43cd37)return wa2_uv_batch(s,m,budget,limit,return_pc);
    if(s.eip==0x46e477 || s.eip==0x46ea23 || s.eip==0x464aa1)return wa2_legacy_vorbis_batch(s,m,budget,limit,return_pc);
    if(s.eip==0x43bde0)return wa2_vertex_batch(s,m,budget,limit,return_pc);
    if(s.eip==0x43b290)return wa2_color_batch(s,m,budget,limit,return_pc);
    if(s.eip==0x47e798)return sse_fill(s,m,budget,limit,return_pc);
    if(s.eip==0x442300)return wa2_sort_shift(s,m,budget,limit,return_pc);
    if(s.eip==0x437040)return lzss_tokens(s,m,budget,limit,return_pc);
    if(wa2_fp_sequence_entry(s.eip))return wa2_fp_sequence_batch(s,m,budget,limit,return_pc);
    constexpr U32 begin=0x4370c0,end=0x4370f1,ring=0xa7cdd0;
    static constexpr std::uint8_t code[]={59,108,36,44,127,45,139,200,129,225,255,15,0,0,138,137,208,205,167,0,139,222,129,227,255,15,0,0,136,139,208,205,167,0,139,92,36,32,136,15,74,64,70,71,69,133,210,125,207};
    if(s.eip!=begin || limit<17 || budget<17 || (return_pc>=begin && return_pc<=end) || s.r[ESP]>0xffffffc0u)return 0;
    const auto signed_value=[](U32 v){return std::bit_cast<std::int32_t>(v);};
    U32 count=std::min<std::uint64_t>(limit/17,budget/17),saved_ebx{};
    const auto sp=s.r[ESP],destination=s.r[EDI];
    auto overlaps=[](std::uint64_t a,std::uint64_t n,std::uint64_t b,std::uint64_t k){return a<b+k && b<a+n;};
    try{
        if(!m.matches_code(begin,code))return 0;
        const auto maximum=signed_value(m.load(sp+0x2c,32)),written=signed_value(s.r[EBP]);
        if(written>maximum || s.r[EDX]==0x80000000u)return 0;
        count=U32(std::min<std::uint64_t>(count,std::uint64_t(std::int64_t(maximum)-written)+1));
        count=std::min(count,signed_value(s.r[EDX])>=0?s.r[EDX]+1:1u);
        if(!count || overlaps(destination,count,ring,4096) || overlaps(destination,count,sp,48) || overlaps(sp,48,ring,4096))return 0;
        // Preflight only enables the fast path. A failure falls back to the
        // original instruction bodies, preserving the exact partial state and
        // fault PC instead of faulting early in the batch.
        m.check(ring,4096,Memory::Read|Memory::Write);m.check(destination,count,Memory::Write);
        saved_ebx=m.load(sp+0x20,32);
    }catch(const GuestFault&){return 0;}
    U32 last_ecx{};
    for(U32 n=0;n<count;++n){
        const auto source=(s.r[EAX]+n)&4095u;
        const auto value=m.load(ring+source,8);
        last_ecx=(source&0xf00u)|value;
        m.store(ring+((s.r[ESI]+n)&4095u),value,8);
        m.store(destination+n,value,8);
    }
    s.r[EAX]+=count;s.r[ECX]=last_ecx;s.r[EDX]-=count;s.r[EBX]=saved_ebx;
    s.r[ESI]+=count;s.r[EDI]+=count;s.r[EBP]+=count;
    // INC EBP defines AF before TEST EDX leaves its value undefined but intact.
    incdec(s,s.r[EBP]-1,32,false);logic(s,s.r[EDX],32);
    s.eip=signed_value(s.r[EDX])>=0?begin:end;budget-=std::uint64_t(count)*17;
    return count*17;
}
static unsigned dispatch_fast(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc){
    if(wa2_native_entry(s.eip))if(const auto count=wa2_native_batch(s,m,budget,limit,return_pc))return count;
    return wa2_static_batch(s,m,budget,limit,return_pc);
}
void install_wa2_fast_paths(Process& p){
    // AOT blocks execute substantially more cheaply than individual guest
    // statements. Keep main-thread boundaries out of their short loops; audio
    // workers retain their own slices and native-event/deadline checks.
    p.state().main_poll_interval=p.options.profile_execution?4096:32768;
    const auto* exit_sampling=std::getenv("WA2_NATIVE_EXITS");
    p.state().sample_fallback=p.options.profile_execution || (exit_sampling && std::string_view(exit_sampling)=="1");
    wa2_native_profiling(p.state().sample_fallback);
    p.state().fast_base=0x400000;p.state().fast_entries.resize(0xb0000);
    static constexpr U32 specialized_entries[]={
#include "specialized_entries.inc"
    };
    for(const U32 pc:specialized_entries)p.state().fast_entries[pc-p.state().fast_base]=1;
    for(const auto& sequence:wa2_fp_sequences())p.state().fast_entries.at(sequence.pc-p.state().fast_base)=1;
    for(const auto pc:wa2_native_entries())p.state().fast_entries.at(pc-p.state().fast_base)=1;
    p.state().fast_step=dispatch_fast;
    p.state().reports["native_execution"]=wa2_native_report;
    p.state().reports["instruction_fallback"]=[&p]{
        std::vector<std::pair<std::uint64_t,std::pair<U32,U32>>> hot;
        std::uint64_t samples=0;for(const auto& [position,count]:p.state().fallback_samples){hot.emplace_back(count,position);samples+=count;}
        std::sort(hot.rbegin(),hot.rend());std::ostringstream out;
        out<<"{\"enabled\":"<<(p.state().sample_fallback?"true":"false")<<",\"sample_interval\":1021,\"samples\":"<<samples<<",\"attempts\":[";
        for(unsigned i=0;i<std::min<std::size_t>(hot.size(),128);++i){if(i)out<<',';
            out<<"{\"thread\":"<<hot[i].second.first<<",\"pc\":"<<hot[i].second.second<<",\"samples\":"<<hot[i].first<<'}';}
        out<<"]}";return out.str();
    };
    p.state().reports["native_cpu_kernels"]=[]{return "{"+wa2_sort_statistics()+",\"lzss_linear_batches\":"+std::to_string(lzss_linear_batches)+",\"lzss_linear_bytes\":"+std::to_string(lzss_linear_bytes)+",\"pak_linear_batches\":"+std::to_string(pak_linear_batches)+",\"pak_linear_bytes\":"+std::to_string(pak_linear_bytes)+"}";};
    p.state().reports["static_batches"]=[&p]{
        std::ostringstream out;out<<"{\"entry\":4419776,\"main_poll_interval\":"<<p.state().main_poll_interval<<",\"batches\":"<<p.state().fast_batches<<",\"guest_instructions\":"<<p.state().fast_instructions<<",\"profile\":[";
        bool first=true;for(const auto& [pc,v]:p.state().fast_profile){if(!first)out<<',';first=false;out<<"{\"entry\":"<<pc<<",\"attempts\":"<<v[0]<<",\"batches\":"<<v[1]<<",\"instructions\":"<<v[2]<<",\"last_fpu_control\":"<<v[3]<<",\"last_limit\":"<<v[4]<<'}';}
        out<<"]}";return out.str();
    };
}
}
