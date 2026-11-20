#pragma once
#include <winrecomp/runtime.hpp>
#include <algorithm>
#include <cstring>

namespace wa2::aot {
struct SaveThumbnailCounts {std::uint64_t copies{},bytes{};};
inline thread_local SaveThumbnailCounts save_thumbnail_counts;
inline thread_local bool save_thumbnail_reference{};
struct SaveThumbnailReferenceScope {
    bool previous=save_thumbnail_reference;
    SaveThumbnailReferenceScope(){save_thumbnail_reference=true;}
    ~SaveThumbnailReferenceScope(){save_thumbnail_reference=previous;}
};
struct SaveThumbnailSource {
    wr::U32 begin;
    unsigned frame_register;std::int32_t frame_delta;
    unsigned frame_bytes,pitch,height,from,to,row_register,row_offset;
};
inline constexpr SaveThumbnailSource main_thumbnail_source{0x43feb6,wr::ESP,0,0x28,0x10,0x24,wr::EDI,wr::EBP,wr::EBX,0};

// The slot preview uploader copies 128 packed XRGB words per row. Leave
// the final pixel and both loop exits to the source instructions, so all
// registers and arithmetic flags have precisely their original values.
inline bool copy_save_thumbnail(wr::Cpu& cpu,wr::Memory& memory,wr::Memory::ExecutionScope& scope,const SaveThumbnailSource& source=main_thumbnail_source){
    constexpr unsigned width=128,height=128,pitch=width*4;
    const auto wide_frame=std::int64_t(cpu.r[source.frame_register])+source.frame_delta;
    constexpr auto limit=std::uint64_t{1}<<32;
    if(save_thumbnail_reference || cpu.eip!=source.begin || cpu.r[wr::EAX]!=width || wide_frame<0 || std::uint64_t(wide_frame)+source.frame_bytes>limit)return false;
    const auto stack=wr::U32(wide_frame),from=cpu.r[source.from],to=cpu.r[source.to];
    unsigned row{};
    try{
        if(memory.load(stack+source.pitch,32)!=pitch || memory.load(stack+source.height,32)!=height)return false;
        row=source.row_register<8?cpu.r[source.row_register]:memory.load(stack+source.row_offset,32);
    }catch(const wr::GuestFault&){return false;}
    if(row>=height)return false;
    const unsigned bytes=(height-row)*pitch;
    if(std::uint64_t(from)+bytes>limit || std::uint64_t(to)+bytes>limit)return false;
    const auto overlaps=[](wr::U32 a,unsigned as,wr::U32 b,unsigned bs){
        return std::uint64_t(a)<std::uint64_t(b)+bs && std::uint64_t(b)<std::uint64_t(a)+as;
    };
    if(overlaps(from,bytes,to,bytes) || overlaps(to,bytes,stack,source.frame_bytes) || overlaps(from,bytes,stack,source.frame_bytes))return false;
    // The paged copy needs indexed backing, including for its first and last
    // partial pages. Decline unindexed mappings and faulting ranges before
    // writing anything so the source loop exposes its precise fault prefix.
    const auto admitted=[&](wr::U32 start,unsigned size,unsigned access){
        const auto end=std::uint64_t(start)+size;
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
    if(!admitted(from,bytes,wr::Memory::Read) || !admitted(to,bytes,wr::Memory::Write) ||
       (source.row_register>=8 && !admitted(stack+source.row_offset,4,wr::Memory::Write)))return false;
    const unsigned prefix=bytes-4;
    if(auto* linear=scope.linear_base())std::memcpy(linear+to,linear+from,prefix);
    else for(unsigned offset=0;offset<prefix;){
        const auto chunk=scope.readable(from+offset,std::min(prefix-offset,4096u-((to+offset)&4095u)));
        scope.write_bytes(to+offset,chunk);offset+=unsigned(chunk.size());
    }
    if(source.row_register<8)cpu.r[source.row_register]=height-1;else scope.store<32>(stack+source.row_offset,height-1);
    cpu.r[wr::ECX]=width-1;cpu.r[source.from]+=prefix;cpu.r[source.to]+=prefix;
    ++save_thumbnail_counts.copies;save_thumbnail_counts.bytes+=bytes;
    return true;
}
}
