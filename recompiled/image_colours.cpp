#include "image_colours.hpp"
#include <array>
#include <algorithm>
#include <cstring>

namespace wa2::aot {namespace {
using Pages=const wr::Memory::NativeIndex* const*;
thread_local ImageColourCounts counts;
constexpr std::uint64_t Limit=std::uint64_t{1}<<32;
constexpr unsigned MaximumBytes=64u*1024*1024;
const wr::Memory::NativePage* page_at(Pages pages,wr::U32 at){
    const auto* index=pages[at>>22];return index?&(*index)[(at>>12)&1023]:nullptr;
}
bool admitted(Pages pages,wr::U32 at,unsigned size,unsigned access){
    const auto end=std::uint64_t(at)+size;if(end>Limit)return false;
    for(std::uint64_t cursor=at;cursor<end;){
        const auto* page=page_at(pages,wr::U32(cursor));const auto offset=unsigned(cursor&4095);
        const auto bytes=unsigned(std::min<std::uint64_t>(4096-offset,end-cursor));
        if(!page || !page->data || (page->permissions&access)!=access || offset+bytes>page->length ||
           ((access&wr::Memory::Write) && (page->permissions&wr::Memory::Execute)))return false;
        cursor+=bytes;
    }
    return true;
}
bool overlaps(wr::U32 a,unsigned as,wr::U32 b,unsigned bs){
    return std::uint64_t(a)<std::uint64_t(b)+bs && std::uint64_t(b)<std::uint64_t(a)+as;
}
using Tables=std::array<std::array<std::uint8_t,256>,3>;
template<unsigned Step>void convert(std::uint8_t* data,unsigned pixels,const Tables& table){
    for(unsigned n=0;n<pixels;++n,data+=Step){
        data[2]=table[2][data[2]];data[1]=table[1][data[1]];data[0]=table[0][data[0]];
    }
}
template<unsigned Step>void convert_pages(wr::Memory::ExecutionScope& scope,wr::U32 at,unsigned pixels,const Tables& table){
    if(auto* base=scope.linear_base()){convert<Step>(base+at,pixels,table);return;}
    // Backing pages need not be adjacent. Only a pixel crossing a backing-page
    // boundary uses checked byte operations; all other pixels use native RAM.
    while(pixels){
        const auto* page=page_at(scope.native_pages(),at);const auto offset=at&4095;
        const auto available=page->length-offset;
        const auto amount=available<3?0:std::min(pixels,1+(available-3)/Step);
        if(amount){convert<Step>(page->data+offset,amount,table);at+=amount*Step;pixels-=amount;}
        else{
            for(unsigned channel:{2u,1u,0u})scope.store<8>(at+channel,table[channel][scope.load<8>(at+channel)]);
            at+=Step;--pixels;
        }
    }
}
}
bool native_image_colours(wr::Cpu& cpu,wr::Memory& memory){
#if defined(WA2_AOT_DIAGNOSTICS) && WA2_AOT_DIAGNOSTICS
    if(!image_colours_enabled)return false;
#endif
    unsigned step{},counter{},scratch{};bool composed{},single{};wr::U32 exit{};
    switch(cpu.eip){
    case 0x439156:step=4;composed=true;exit=0x439193;break;
    case 0x4391de:step=4;exit=0x439208;break;
    case 0x43922a:step=4;single=true;exit=0x439257;break;
    case 0x4392e6:step=3;composed=true;exit=0x439323;break;
    case 0x43936e:step=3;exit=0x439398;break;
    case 0x4393ba:step=3;single=true;exit=0x4393e7;break;
    default:return false;
    }
    counter=composed?wr::EDX:single?wr::ESI:wr::EBP;scratch=composed?wr::EBX:wr::EDX;
    const auto count=cpu.r[counter],first=cpu.r[wr::EBX];
    // Small palettes retain the original loop. A negative source count, a
    // wrapping next-pixel register or an oversized rectangle must decline.
    if(count<64 || count>MaximumBytes/step || std::uint64_t(first)+1+std::uint64_t(count)*step>=Limit)return false;
    ++counts.attempts;
    const auto bytes=count*step-(step-3);
    std::array<wr::U32,3> addresses=composed?std::array{cpu.r[wr::EBP],cpu.r[wr::EDI],cpu.r[wr::ESI]}:
        single?std::array{cpu.r[wr::ECX],cpu.r[wr::ECX],cpu.r[wr::ECX]}:
               std::array{cpu.r[wr::EDI],cpu.r[wr::ESI],cpu.r[wr::ECX]};
    auto scope=memory.execution_scope();const auto pages=scope.native_pages();
    if(!admitted(pages,first,bytes,wr::Memory::Read|wr::Memory::Write))return false;
    for(auto address:addresses)if(!admitted(pages,address,256,wr::Memory::Read) || overlaps(first,bytes,address,256))return false;
    if(composed && (!admitted(pages,cpu.r[wr::ECX],256,wr::Memory::Read) || overlaps(first,bytes,cpu.r[wr::ECX],256)))return false;
    Tables tables;std::array<std::uint8_t,256> final;
    if(composed)memory.copy_out(cpu.r[wr::ECX],final);
    // Compose the two read-only lookups once, instead of once per pixel. No
    // table is retained between images, so edits always take effect at once.
    for(unsigned channel=0;channel<3;++channel){
        memory.copy_out(addresses[channel],tables[channel]);
        if(composed)for(auto& value:tables[channel])value=final[value];
    }
    if(step==4)convert_pages<4>(scope,first,count,tables);else convert_pages<3>(scope,first,count,tables);
    cpu.r[scratch]=scope.load<8>(first+(count-1)*step);
    cpu.r[wr::EAX]=first+1+count*step;cpu.r[counter]=wr::sub(cpu,1,1,32);cpu.eip=exit;
    ++counts.completed;counts.pixels+=count;return true;
}
ImageColourCounts image_colour_counts(){return counts;}
}
