#include "image_chroma.hpp"
#include "../engine/image_chroma.hpp"
#include <algorithm>
#include <cstring>
namespace wa2::aot {namespace {
thread_local ImageChromaCounts counts;
constexpr std::uint64_t Limit=std::uint64_t{1}<<32;
using Pages=const wr::Memory::NativeIndex* const*;
const wr::Memory::NativePage* page_at(Pages pages,wr::U32 at){const auto* index=pages[at>>22];return index?&(*index)[(at>>12)&1023]:nullptr;}
bool admitted(Pages pages,wr::U32 at,unsigned bytes,unsigned access){
    const auto end=std::uint64_t(at)+bytes;if(end>Limit)return false;
    for(std::uint64_t cursor=at;cursor<end;){const auto* page=page_at(pages,wr::U32(cursor));const auto offset=unsigned(cursor&4095),size=unsigned(std::min<std::uint64_t>(4096-offset,end-cursor));
        if(!page || !page->data || (page->permissions&access)!=access || offset+size>page->length || ((access&wr::Memory::Write) && (page->permissions&wr::Memory::Execute)))return false;cursor+=size;
    }return true;
}
bool overlaps(wr::U32 a,unsigned as,wr::U32 b,unsigned bs){return std::uint64_t(a)<std::uint64_t(b)+bs && std::uint64_t(b)<std::uint64_t(a)+as;}
}
bool native_image_chroma(wr::Cpu& cpu,wr::Memory& memory){
    unsigned step{};bool red{},grey{},pushes{};wr::U32 exit{};
    switch(cpu.eip){
    case 0x43884d:step=4;red=true;grey=true;exit=0x438889;break;
    case 0x438898:step=4;grey=true;exit=0x4388d5;break;
    case 0x438906:step=4;red=true;exit=0x438971;break;
    case 0x438984:step=4;exit=0x4389f1;break;
    case 0x438a23:step=3;grey=true;exit=0x438a65;break;
    case 0x438a85:step=3;pushes=true;exit=0x438af1;break;
    default:return false;
    }
    ++counts.attempts;
    const auto count=cpu.r[grey?wr::EDX:wr::ECX],stack=cpu.r[wr::ESP];
    if(count<64 || count>(64u*1024*1024)/step || stack<8 || std::uint64_t(stack)+48>Limit)return false;
    auto scope=memory.execution_scope();const auto pages=scope.native_pages();
    if(!admitted(pages,stack-8,56,wr::Memory::Read|wr::Memory::Write))return false;
    const auto data=scope.load<32>(stack+(!grey && step==4?0x18:0x10));
    const auto bytes=count*step-(step-3);
    if(std::uint64_t(data)+2+std::uint64_t(count)*step>=Limit || !admitted(pages,data,bytes,wr::Memory::Read|wr::Memory::Write) || overlaps(data,bytes,stack-8,56))return false;
    wa2::image::ChromaTables tables;wr::U32 channel{},complement{};
    if(!grey){const auto scaled=cpu.r[step==4?wr::ESI:wr::EDI];
        if(!scaled || scaled>255*257 || scaled%257)return false;
        channel=0xa83db0+scaled;complement=scope.load<32>(stack+(step==4?0x10:0x08));
        if(!admitted(pages,channel,256,wr::Memory::Read) || !admitted(pages,complement,256,wr::Memory::Read) ||
            overlaps(data,bytes,channel,256) || overlaps(data,bytes,complement,256) || overlaps(stack-8,56,channel,256) || overlaps(stack-8,56,complement,256))return false;
        memory.copy_out(channel,tables.channel);memory.copy_out(complement,tables.grey);
    }
    const auto last=data+(count-1)*step;
    const unsigned a=scope.load<8>(last),b=scope.load<8>(last+1),c=scope.load<8>(last+2);
    const auto grey_value=(a*(red?77:28)+b*151+c*(red?28:77))>>8;
    if(pushes){scope.store<32>(stack-4,cpu.r[wr::EBX]);scope.store<32>(stack-8,cpu.r[wr::EBP]);cpu.r[wr::ESP]=stack-8;}
    if(auto* base=scope.linear_base())wa2::image::convert_chroma({base+data,bytes},count,step,red,grey?nullptr:&tables);
    else{auto cursor=data,remaining=count;
        while(remaining){const auto* page=page_at(pages,cursor);const auto offset=cursor&4095,available=page->length-offset;
            const auto amount=available<3?0:std::min(remaining,1+(available-3)/step);
            if(amount){wa2::image::convert_chroma({page->data+offset,amount*step-(step-3)},amount,step,red,grey?nullptr:&tables);cursor+=amount*step;remaining-=amount;}
            else{std::array<std::uint8_t,3> pixel;for(unsigned n=0;n<3;++n)pixel[n]=std::uint8_t(scope.load<8>(cursor+n));wa2::image::convert_chroma(pixel,1,step,red,grey?nullptr:&tables);
                for(unsigned n=0;n<3;++n)scope.store<8>(cursor+n,pixel[n]);cursor+=step;--remaining;}
        }
    }
    cpu.r[wr::EAX]=data+(grey?2:1)+count*step;
    if(grey){cpu.r[wr::EDX]=0;cpu.r[wr::ECX]=grey_value;
        cpu.r[wr::ESI]=step==3?c*77:red?a*77+b*151:b*151;
        if(step==4)cpu.r[wr::EDI]=(red?c:a)*7;
    }else{const auto contribution=tables.grey[grey_value];
        cpu.r[wr::EBX]=complement;
        if(red){cpu.r[wr::EDI]=b;cpu.r[wr::EBP]=a;cpu.r[wr::EDX]=contribution;cpu.r[wr::ECX]=std::uint8_t(tables.channel[a]+contribution);}
        else{cpu.r[step==4?wr::EDI:wr::ESI]=a;cpu.r[wr::EBP]=b;cpu.r[wr::EDX]=std::uint8_t(tables.channel[a]+contribution);cpu.r[wr::ECX]=contribution;}
        scope.store<32>(cpu.r[wr::ESP]+0x20,0);
    }
    (void)wr::sub(cpu,1,1,32);cpu.eip=exit;++counts.completed;counts.pixels+=count;if(grey)counts.grey_pixels+=count;return true;
}
ImageChromaCounts image_chroma_counts(){return counts;}
}
