#pragma once
#include <winrecomp/runtime.hpp>
#include <algorithm>
#include <cstring>

namespace wa2::aot {
struct SaveThumbnailCounts {std::uint64_t copies{},bytes{};};
inline thread_local SaveThumbnailCounts save_thumbnail_counts;

// The slot preview uploader copies 128 packed XRGB words per row. Leave
// the final pixel and both loop exits to the source instructions, so all
// registers and arithmetic flags have precisely their original values.
inline bool copy_save_thumbnail(wr::Cpu& cpu,wr::Memory& memory,wr::Memory::ExecutionScope& scope){
    constexpr unsigned width=128,height=128,pitch=width*4;
    const auto row=cpu.r[wr::EBX],stack=cpu.r[wr::ESP],from=cpu.r[wr::EDI],to=cpu.r[wr::EBP];
    constexpr auto limit=std::uint64_t{1}<<32;
    if(cpu.eip!=0x43feb6 || cpu.r[wr::EAX]!=width || row>=height || std::uint64_t(stack)+0x28>limit)return false;
    const unsigned bytes=(height-row)*pitch;
    if(std::uint64_t(from)+bytes>limit || std::uint64_t(to)+bytes>limit)return false;
    const auto overlaps=[](wr::U32 a,unsigned as,wr::U32 b,unsigned bs){
        return std::uint64_t(a)<std::uint64_t(b)+bs && std::uint64_t(b)<std::uint64_t(a)+as;
    };
    if(overlaps(from,bytes,to,bytes) || overlaps(to,bytes,stack,0x28) || overlaps(from,bytes,stack,0x28))return false;
    try{
        if(memory.load(stack+0x10,32)!=pitch || memory.load(stack+0x24,32)!=height)return false;
    }catch(const wr::GuestFault&){return false;}
    // The paged copy needs indexed backing, including for its first and last
    // partial pages. Decline unindexed mappings and faulting ranges before
    // writing anything so the source loop exposes its precise fault prefix.
    const auto admitted=[&](wr::U32 start,unsigned access){
        const auto end=std::uint64_t(start)+bytes;
        for(std::uint64_t at=start;at<end;){
            const auto* table=scope.native_pages()[at>>22];if(!table)return false;
            const auto& page=(*table)[(at>>12)&1023];const unsigned offset=unsigned(at&4095);
            const auto span=std::min<std::uint64_t>(4096-offset,end-at);
            if(!page.data || offset+span>page.length || !(page.permissions&access) ||
               (access==wr::Memory::Write && (page.permissions&wr::Memory::Execute)))return false;
            at+=span;
        }
        return true;
    };
    if(!admitted(from,wr::Memory::Read) || !admitted(to,wr::Memory::Write))return false;
    const unsigned prefix=bytes-4;
    if(auto* linear=scope.linear_base())std::memcpy(linear+to,linear+from,prefix);
    else for(unsigned offset=0;offset<prefix;){
        const auto chunk=scope.readable(from+offset,std::min(prefix-offset,4096u-((to+offset)&4095u)));
        scope.write_bytes(to+offset,chunk);offset+=unsigned(chunk.size());
    }
    cpu.r[wr::EBX]=height-1;cpu.r[wr::ECX]=width-1;
    cpu.r[wr::EDI]+=prefix;cpu.r[wr::EBP]+=prefix;
    ++save_thumbnail_counts.copies;save_thumbnail_counts.bytes+=bytes;
    return true;
}
}
