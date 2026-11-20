// Common unmetered form of port/sprite_sort.cpp's checked source insertion.
// Main and Special differ only in their source ABI/data bindings.
#include "source_sort.hpp"
#include <winrecomp/integer.hpp>
#include <algorithm>
#include <bit>
#include <cstring>
namespace wa2::aot {namespace {
thread_local SourceSortCounts counts;
bool overlap(std::uint64_t a,std::uint64_t n,std::uint64_t b,std::uint64_t k){return a<b+k && b<a+n;}
}
SourceSortCounts source_sort_counts(){return counts;}
bool source_sort_full(wr::Cpu& s,wr::Memory& m,const SortSource& source){
    using namespace wr;
    if(source_sort_reference || s.eip!=source.begin || source.frame_register>=8)return false;
    const U32 first_index=s.r[EBX]+1;
    if(first_index>=2048 || std::uint64_t(source.objects)+2047*256+8>0x100000000ull ||
       s.r[EAX]!=source.objects+first_index*256)return false;
    const auto frame=std::int64_t(s.r[source.frame_register]);
    const auto at=[&](std::int32_t offset){return frame+offset;};
    if(at(source.first)<0 || at(source.last)>0x100000000ll || source.first>=source.last ||
       at(source.pointer)<at(source.first) || at(source.pointer)+4>at(source.last) ||
       at(source.keys)<at(source.first) || at(source.keys)+4096>at(source.last) ||
       at(source.ids)!=at(source.keys)+4096 || at(source.ids)+4096>at(source.last))return false;
    const auto pointer=U32(at(source.pointer)),keys_at=U32(at(source.keys)),ids_at=U32(at(source.ids));
    const auto object=source.objects+first_index*256,bytes=(2047-first_index)*256+8;
    const auto scratch_size=std::int64_t(source.last)-source.first;
    if(overlap(at(source.first),scratch_size,object-1,bytes) ||
       overlap(at(source.first),scratch_size,source.mask,4))return false;
    auto scope=m.execution_scope();auto* linear=scope.linear_base();
    const auto checked=[&](U32 address,unsigned size,unsigned access){
        if(std::uint64_t(address)+size>0x100000000ull)return false;
        for(std::uint64_t cursor=address,end=cursor+size;cursor<end;){
            const auto* table=scope.native_pages()[cursor>>22];if(!table)return false;
            const auto& page=(*table)[(cursor>>12)&1023];const auto offset=unsigned(cursor&4095);
            const auto count=unsigned(std::min<std::uint64_t>(end-cursor,4096-offset));
            if(!page.data || (page.permissions&access)!=access || ((access&Memory::Write) && (page.permissions&Memory::Execute)) ||
               offset+count>page.length || (linear && page.data!=linear+(cursor&~4095ull)))return false;
            cursor+=count;
        }return true;
    };
    std::array<std::uint16_t,2048> keys,ids;U32 mask;
    try{
        if(!checked(pointer,4,Memory::Read|Memory::Write) || !checked(keys_at,8192,Memory::Read|Memory::Write) ||
           !checked(object-1,bytes,Memory::Read) || !checked(source.mask,4,Memory::Read))return false;
        if(scope.load<32>(pointer)!=object)return false;
        m.copy_out(keys_at,{reinterpret_cast<std::uint8_t*>(keys.data()),sizeof(keys)});
        m.copy_out(ids_at,{reinterpret_cast<std::uint8_t*>(ids.data()),sizeof(ids)});
        mask=scope.load<32>(source.mask);
    }catch(const GuestFault&){++counts.declined;return false;}
    const auto less=[](std::uint16_t x,std::uint16_t y){return std::bit_cast<std::int16_t>(x)<std::bit_cast<std::int16_t>(y);};
    unsigned unordered{};for(unsigned n=1;n<keys.size();++n)unordered|=unsigned(less(keys[n],keys[n-1]));
    if(unordered){++counts.declined;return false;}
    auto esi=s.r[ESI],edi=s.r[EDI],edx=s.r[EDX];bool changed{};std::uint64_t moved{};
    U32 index=first_index;
    // A linear source span was checked once under the live memory scope.
    // Sparse backings retain the same checked fixed-width reads.
    const auto byte=[&](U32 address){return linear?unsigned(linear[address]):scope.load<8>(address);};
    const auto word=[&](U32 address){return linear?U32(linear[address])|(U32(linear[address+1])<<8):scope.load<16>(address);};
    const auto dword=[&](U32 address){return linear?U32(linear[address])|(U32(linear[address+1])<<8)|(U32(linear[address+2])<<16)|(U32(linear[address+3])<<24):scope.load<32>(address);};
    for(;index<2048;++index){const auto item=source.objects+index*256;
        if(!byte(item-1) || !byte(item) || !(dword(item+3)&mask))continue;
        const auto key=std::uint16_t(word(item+1));
        const auto position=unsigned(std::lower_bound(keys.begin(),keys.begin()+index+1,key,less)-keys.begin());
        const bool found=position<=index,shift=found && keys[position]!=0x7530;
        edi=position;esi=index+1;edx=(edx&0xffff0000u)|(found?key:keys[index]);
        if(!found)continue;
        if(shift){esi=index-1;if(position<index){
            const auto size=(index-position)*2;
            std::memmove(keys.data()+position+1,keys.data()+position,size);
            std::memmove(ids.data()+position+1,ids.data()+position,size);
            esi=position-1;moved+=index-position;
        }}
        keys[position]=key;ids[position]=std::uint16_t(index);changed=true;
    }
    if(changed){m.copy_in(keys_at,{reinterpret_cast<const std::uint8_t*>(keys.data()),sizeof(keys)});
        m.copy_in(ids_at,{reinterpret_cast<const std::uint8_t*>(ids.data()),sizeof(ids)});}
    s.r[EAX]=source.objects+index*256;s.r[EBX]=index-1;s.r[ECX]=index;s.r[ESI]=esi;s.r[EDI]=edi;s.r[EDX]=edx;
    scope.store<32>(pointer,s.r[EAX]);sub(s,index,2048,32);s.eip=source.end;
    ++counts.completed;counts.objects+=2048-first_index;counts.moves+=moved;return true;
}
}
