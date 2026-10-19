#include "native_string.hpp"
#include "work_samples.hpp"
#include <cstring>
#include <optional>

namespace wa2::aot {
namespace {
using Pages=const wr::Memory::NativeIndex* const*;
constexpr std::uint64_t AddressLimit=std::uint64_t{1}<<32;
// Scene images routinely exceed 1 MiB. Declining their REP or clear loop
// re-enters the element-by-element checked path for most of an image.
// Keep the same bounded admission as the other native image operations.
constexpr unsigned MaximumBytes=64u*1024*1024;

const wr::Memory::NativePage* page_at(Pages pages,wr::U32 at){
    const auto* index=pages[at>>22];return index?&(*index)[(at>>12)&1023]:nullptr;
}

bool admitted(Pages pages,wr::U32 at,unsigned bytes,unsigned access){
    const auto end=std::uint64_t(at)+bytes;
    if(end>AddressLimit)return false;
    for(std::uint64_t cursor=at;cursor<end;){
        const auto* page=page_at(pages,wr::U32(cursor));
        const auto offset=unsigned(cursor&4095);
        const auto amount=unsigned(std::min<std::uint64_t>(4096-offset,end-cursor));
        if(!page || !page->data || (page->permissions&access)!=access ||
           (access==wr::Memory::Write && (page->permissions&wr::Memory::Execute)) ||
           offset+amount>page->length)return false;
        cursor+=amount;
    }
    return true;
}

template<unsigned Step>
void patterned_fill(std::uint8_t* destination,unsigned bytes,unsigned phase,
                    const std::array<std::uint8_t,4>& pattern){
    while(bytes && phase){*destination++=pattern[phase];phase=(phase+1)&(Step-1);--bytes;}
    while(bytes>=Step){std::memcpy(destination,pattern.data(),Step);destination+=Step;bytes-=Step;}
    while(bytes--)*destination++=pattern[phase++];
}

void fill_range(wr::Memory::ExecutionScope& scope,wr::U32 destination,unsigned bytes,wr::U32 word){
    const std::array<std::uint8_t,4> pattern{
        std::uint8_t(word),std::uint8_t(word>>8),std::uint8_t(word>>16),std::uint8_t(word>>24)};
    if(auto* base=scope.linear_base())patterned_fill<4>(base+destination,bytes,0,pattern);
    else for(unsigned done=0;done<bytes;){
        const auto di=destination+done,amount=std::min(bytes-done,4096-(di&4095));
        patterned_fill<4>(page_at(scope.native_pages(),di)->data+(di&4095),amount,done&3,pattern);done+=amount;
    }
}
}

bool try_bulk_string(wr::Cpu& cpu,wr::Memory& memory,unsigned kind,unsigned width,
                     unsigned address_width,unsigned repeat,unsigned segment){
    if(kind>1 || (width!=8 && width!=16 && width!=32) || address_width!=32 ||
       (repeat!=1 && repeat!=2) || segment)return false;
    const auto count=cpu.r[wr::ECX],step=width/8;
    if(!count)return true;
    if(count>MaximumBytes/step)return false;
    if(current_work_samples)++current_work_samples->string_bulk_attempts;
    const auto bytes=count*step;const bool backwards=(cpu.flags&wr::DF)!=0;
    // Backwards MOVS/STOS address the current element, not its last byte.
    // Reject a wrapped accessed range, but allow the final index to wrap.
    const auto low=[&](wr::U32 at)->std::optional<wr::U32>{
        if(!backwards)return at;
        if(at<bytes-step || std::uint64_t(at)+step>AddressLimit)return {};
        return at-(bytes-step);
    };
    const auto target=low(cpu.r[wr::EDI]);
    const auto input=kind==0?low(cpu.r[wr::ESI]):std::optional<wr::U32>{0};
    if(!target || !input)return false;
    const auto source=*input,destination=*target;
    if(kind==0 && source!=destination &&
       std::uint64_t(source)<std::uint64_t(destination)+bytes &&
       std::uint64_t(destination)<std::uint64_t(source)+bytes)return false;
    auto scope=memory.execution_scope();const auto pages=scope.native_pages();
    if(!admitted(pages,destination,bytes,wr::Memory::Write) ||
       (kind==0 && !admitted(pages,source,bytes,wr::Memory::Read)))return false;
    if(kind==0){
        if(source!=destination){
            if(auto* base=scope.linear_base())std::memcpy(base+destination,base+source,bytes);
            else for(unsigned done=0;done<bytes;){
                const auto si=source+done,di=destination+done;
                const auto amount=std::min({bytes-done,4096-(si&4095),4096-(di&4095)});
                std::memcpy(page_at(pages,di)->data+(di&4095),page_at(pages,si)->data+(si&4095),amount);
                done+=amount;
            }
        }
        cpu.r[wr::ESI]+=backwards?0u-bytes:bytes;
    }else{
        auto word=cpu.r[wr::EAX];
        if(step==1)word=(word&255)*0x01010101u;
        else if(step==2)word=(word&65535)*0x00010001u;
        fill_range(scope,destination,bytes,word);
    }
    cpu.r[wr::EDI]+=backwards?0u-bytes:bytes;cpu.r[wr::ECX]=0;
    if(current_work_samples){++current_work_samples->string_bulk_completed;current_work_samples->string_bulk_bytes+=bytes;}
    return true;
}

bool native_vector_clear(wr::Cpu& cpu,wr::Memory& memory){
    // Cached 0x47e798..0x47e7c8: eight aligned MOVDQA stores, LEA +128,
    // DEC ECX and JNZ. The preceding PXOR has already established XMM0=0.
    // All stores complete before the final flags become visible. Declining
    // leaves even alignment/permission/MXCSR faults at the original store PC.
    if(current_work_samples)++current_work_samples->vector_clear_attempts;
    const auto count=cpu.r[wr::ECX],destination=cpu.r[wr::EDI];
    if(!count || count>MaximumBytes/128 || (destination&15) ||
       (cpu.fp.get32(24)&0xffff1f80u)!=0x1f80u || cpu.fp.get64(160) || cpu.fp.get64(168))return false;
    auto scope=memory.execution_scope();const auto bytes=count*128;
    if(!admitted(scope.native_pages(),destination,bytes,wr::Memory::Write))return false;
    fill_range(scope,destination,bytes,0);
    // The checked MOVDQA kernels normalize these FXSAVE fields on the first
    // store. A resume may arrive with noncanonical padding, unlike the normal
    // caller whose preceding PXOR already normalized it.
    cpu.fp.put32(28,0xffff);cpu.fp.clear_x87_padding();
    cpu.r[wr::EDI]+=bytes;cpu.r[wr::ECX]=wr::incdec(cpu,1,32,true);cpu.eip=0x47e7c8;
    if(current_work_samples){++current_work_samples->vector_clear_completed;current_work_samples->vector_clear_bytes+=bytes;}
    return true;
}
}
