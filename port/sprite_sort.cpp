// Compatibility boundary only: ordering/search execute as native C++.
#include "sprite_sort.hpp"
#include "native_backend.hpp"
#include <winrecomp/integer.hpp>
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

namespace wr {
namespace {
#include "sort_signature.inc"
thread_local std::uint64_t sort_linear_batches{},sort_linear_words{};
thread_local std::uint64_t sort_search_batches{},sort_search_objects{},sort_search_moves{};
thread_local std::uint64_t sort_partial_batches{},sort_partial_instructions{};
thread_local bool partial_sort_search_enabled=true;
bool ranges_overlap(std::uint64_t a,std::uint64_t n,std::uint64_t b,std::uint64_t k){return a<b+k && b<a+n;}
}
#if !defined(WA2_AOT_DIAGNOSTICS) || WA2_AOT_DIAGNOSTICS
void wa2_partial_sort_search(bool enabled){partial_sort_search_enabled=enabled;}
#endif
template<bool Metered>
unsigned sort_objects(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc){
    constexpr U32 begin=0x442290,end=0x442374,objects=0xab3fb9,mask_at=0x4c22b0;
    U32 index=s.r[EBX]+1;const auto sp=s.r[ESP];
    if(index>=2048 || sp>0xffffd000u || (Metered && (limit<9 || budget<9)) || (return_pc>=begin && return_pc<end) ||
       s.r[EAX]!=objects+index*256)return 0;
    auto scope=m.execution_scope();const auto* linear=scope.linear_base();
    const auto source=objects+index*256;
    const auto source_bytes=(2047-index)*256+8;
    if(ranges_overlap(sp+0x1c,0x2438-0x1c,source-1,source_bytes) ||
       ranges_overlap(sp+0x1c,0x2438-0x1c,mask_at,4))return 0;
    const auto checked_span=[&](U32 at,unsigned size,unsigned access){
        if(std::uint64_t(at)+size>0x100000000ull)return false;
        for(std::uint64_t cursor=at,end=std::uint64_t(at)+size;cursor<end;){
            const auto* table=scope.native_pages()[cursor>>22];if(!table)return false;
            const auto& page=(*table)[(cursor>>12)&1023];const auto offset=unsigned(cursor&4095);
            const auto amount=unsigned(std::min<std::uint64_t>(end-cursor,4096-offset));
            if(!page.data || (page.permissions&access)!=access || ((access&Memory::Write) && (page.permissions&Memory::Execute)) ||
               offset+amount>page.length || (linear && page.data!=linear+(cursor&~4095ull)))return false;
            cursor+=amount;
        }
        return true;
    };
    std::array<std::uint16_t,2048> keys,ids;
    U32 mask;
    try{
        if(!wa2_native_code_verified(m,begin))return 0;
        if(!checked_span(sp+0x1c,4,Memory::Read|Memory::Write) || !checked_span(sp+0x438,8192,Memory::Read|Memory::Write) ||
           !checked_span(source-1,source_bytes,Memory::Read) || !checked_span(mask_at,4,Memory::Read))return 0;
        const auto current=scope.load<32>(sp+0x1c);if(current!=source)return 0;
        m.copy_out(sp+0x438,{reinterpret_cast<std::uint8_t*>(keys.data()),sizeof(keys)});
        m.copy_out(sp+0x1438,{reinterpret_cast<std::uint8_t*>(ids.data()),sizeof(ids)});
        mask=scope.load<32>(mask_at);
    }catch(const GuestFault&){return 0;}
    const auto less=[](std::uint16_t a,std::uint16_t b){return std::bit_cast<std::int16_t>(a)<std::bit_cast<std::int16_t>(b);};
    // Unlike the branch-per-element library walk, this integer reduction can
    // use the host compiler's SIMD vectorizer. Signed 16-bit ordering stays
    // identical, including duplicate keys and the 30000 sentinel.
    unsigned unordered=0;
    for(unsigned at=1;at<keys.size();++at)unordered|=unsigned(less(keys[at],keys[at-1]));
    if(unordered)return 0;
    const auto available=Metered?unsigned(std::min<std::uint64_t>(limit,budget)):std::numeric_limits<unsigned>::max();
    unsigned executed=0,completed=0;std::uint64_t moved=0;
    // A whole insertion may exceed a scheduler slice. Keep the same search
    // boundary and instruction charge while finding its result in C++.
    U32 partial_pc{};bool partial_compare{};unsigned partial_steps{},partial_charge{};
    std::uint16_t partial_key{};
    auto esi=s.r[ESI],edi=s.r[EDI],edx=s.r[EDX];bool changed=false;
    while(index<2048){
        const auto object=objects+index*256;
        unsigned cost=9,position=0;bool active=false,found=false,shift=false;std::uint16_t key{};
        if(scope.load<8>(object-1)){
            cost=11;
            if(scope.load<8>(object)){
                const auto attributes=scope.load<32>(object+3);cost=14;
                if(attributes&mask){
                    active=true;key=std::uint16_t(scope.load<16>(object+1));
                    position=unsigned(std::lower_bound(keys.begin(),keys.begin()+index+1,key,less)-keys.begin());
                    found=position<=index;
                    shift=found && keys[position]!=0x7530;
                    cost=found?(shift?36+9*index:33+9*position):29+9*index;
                }
            }
        }
        if constexpr(Metered)if(cost>available-executed){
            const auto left=available-executed;
            if(partial_sort_search_enabled && active && left>=12){
                esi=index+1;
                const auto search_cost=found?18+9*position:22+9*index;
                if(search_cost<=left){
                    edi=position;edx=(edx&0xffff0000u)|keys[found?position:index];
                    partial_pc=found?0x4422f0:0x442357;
                    partial_compare=found;partial_key=key;
                    partial_steps=found?position:index+1;
                    partial_charge=search_cost;executed+=partial_charge;
                }else{
                    partial_steps=std::min((left-12)/9,index+1);edi=partial_steps;
                    if(partial_steps)edx=(edx&0xffff0000u)|keys[partial_steps-1];
                    partial_pc=partial_steps==index+1?0x4422ee:0x4422c0;
                    partial_charge=12+9*partial_steps;executed+=partial_charge;
                }
            }
            break;
        }
        if(active){
            edi=position;esi=index+1;
            edx=(edx&0xffff0000u)|(found?key:keys[index]);
            if(found){
                if(shift){
                    esi=index-1;
                    if(position<index){
                        const auto bytes=(index-position)*2;
                        std::memmove(keys.data()+position+1,keys.data()+position,bytes);
                        std::memmove(ids.data()+position+1,ids.data()+position,bytes);
                        esi=position-1;moved+=index-position;
                    }
                }
                keys[position]=key;ids[position]=std::uint16_t(index);changed=true;
            }
        }
        if constexpr(Metered)executed+=cost;else ++executed;
        ++completed;++index;
    }
    if(!executed)return 0;
    if(changed){
        m.copy_in(sp+0x438,{reinterpret_cast<const std::uint8_t*>(keys.data()),sizeof(keys)});
        m.copy_in(sp+0x1438,{reinterpret_cast<const std::uint8_t*>(ids.data()),sizeof(ids)});
    }
    s.r[EAX]=objects+index*256;s.r[EBX]=index-1;s.r[ECX]=partial_pc?mask:index;s.r[ESI]=esi;s.r[EDI]=edi;s.r[EDX]=edx;
    // All writes were proven writable and non-executable under this scope.
    // Like native arena stores, they cannot invalidate a code-page proof.
    scope.store<32>(sp+0x1c,s.r[EAX]);
    if(partial_pc){
        ++sort_partial_batches;sort_partial_instructions+=partial_charge;
        if(partial_compare)sub(s,edx&0xffffu,partial_key,16);
        else if(partial_steps)sub(s,edi,esi,32);
        else{ sub(s,0,0,8);logic(s,esi,32); }
        s.eip=partial_pc;
    }else{sub(s,index,2048,32);s.eip=index<2048?begin:end;}
    if constexpr(Metered)budget-=executed;
    ++sort_search_batches;sort_search_objects+=completed;sort_search_moves+=moved;return executed;
}
#if !defined(WA2_AOT_DIAGNOSTICS) || WA2_AOT_DIAGNOSTICS
unsigned wa2_sort_objects(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc){return sort_objects<true>(s,m,budget,limit,return_pc);}
#endif
bool wa2_sort_objects_full(Cpu& s,Memory& m){std::uint64_t unused{};return sort_objects<false>(s,m,unused,0,0)!=0;}
#if !defined(WA2_AOT_DIAGNOSTICS) || WA2_AOT_DIAGNOSTICS
unsigned wa2_sort_shift(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc){
    constexpr U32 begin=0x442300,end=0x44233c;
    if(limit<9 || budget<9 || (return_pc>=begin && return_pc<=end) || s.r[ESP]>0xffffd000u || s.r[ESI]>=2047 || s.r[EDI]>s.r[ESI])return 0;
    const auto count=U32(std::min<std::uint64_t>({limit/9,budget/9,std::uint64_t(s.r[ESI])-s.r[EDI]+1}));
    const auto first=s.r[ESI]-count+1,bytes=count*2,offset=first*2;
    const auto a=s.r[ESP]+0x438+offset,b=s.r[ESP]+0x1438+offset;
    auto scope=m.execution_scope();
    try{if(!wa2_native_code_verified(m,begin) && !m.matches_code(begin,sort_code))return 0;}
    catch(const GuestFault&){return 0;}
    const auto code_free=[&](U32 at){
        for(std::uint64_t page=at&~4095u;page<std::uint64_t(at)+bytes+2;page+=4096){
            const auto* index=scope.native_pages()[page>>22];if(!index)return false;
            const auto& item=(*index)[(page>>12)&1023];
            if(item.permissions&Memory::Execute)return false;
        }
        return true;
    };
    auto* linear=scope.linear_base();
    const auto direct_span=[&](U32 at){
        if(!linear || std::uint64_t(at)+bytes+2>0x100000000ull)return false;
        for(std::uint64_t cursor=at,end=std::uint64_t(at)+bytes+2;cursor<end;){
            const auto* index=scope.native_pages()[cursor>>22];if(!index)return false;
            const auto& item=(*index)[(cursor>>12)&1023];const auto offset=unsigned(cursor&4095);
            const auto amount=unsigned(std::min<std::uint64_t>(end-cursor,4096-offset));
            if((item.permissions&(Memory::Read|Memory::Write|Memory::Execute))!=(Memory::Read|Memory::Write) ||
               offset+amount>item.length || item.data!=linear+(cursor&~4095ull))return false;
            cursor+=amount;
        }
        return true;
    };
    std::uint16_t left_word{},right_word{};
    if(direct_span(a) && direct_span(b)){
        std::memcpy(&left_word,linear+a,2);std::memcpy(&right_word,linear+b,2);
        // Each checked array is at most 4096 bytes; they are 4096 bytes apart.
        // The two rightward moves cannot overlap one another or modify code.
        std::memmove(linear+b+2,linear+b,bytes);std::memmove(linear+a+2,linear+a,bytes);
        ++sort_linear_batches;sort_linear_words+=count*2;
    }else{
        std::array<std::uint8_t,4096> left,right;
        try{m.check(a,bytes+2,Memory::Read|Memory::Write);m.check(b,bytes+2,Memory::Read|Memory::Write);
            if(!code_free(a) || !code_free(b))return 0;
            m.copy_out(a,std::span(left).first(bytes));m.copy_out(b,std::span(right).first(bytes));}
        catch(const GuestFault&){return 0;}
        m.copy_in(b+2,std::span(right).first(bytes));m.copy_in(a+2,std::span(left).first(bytes));
        left_word=std::uint16_t(U32(left[0])|(U32(left[1])<<8));right_word=std::uint16_t(U32(right[0])|(U32(right[1])<<8));
    }
    s.r[EAX]=(s.r[EAX]&0xffff0000u)|left_word;
    s.r[EDX]=(s.r[EDX]&0xffff0000u)|right_word;
    s.r[ESI]-=count;sub(s,s.r[ESI],s.r[EDI],32);
    s.eip=std::bit_cast<std::int32_t>(s.r[ESI])>=std::bit_cast<std::int32_t>(s.r[EDI])?begin:end;
    budget-=std::uint64_t(count)*9;return count*9;
}
#endif
std::string wa2_sort_statistics(){
    return "\"sort_linear_batches\":"+std::to_string(sort_linear_batches)+",\"sort_linear_words\":"+std::to_string(sort_linear_words)+
        ",\"sort_search_batches\":"+std::to_string(sort_search_batches)+",\"sort_search_objects\":"+std::to_string(sort_search_objects)+
        ",\"sort_search_moves\":"+std::to_string(sort_search_moves)+",\"sort_partial_batches\":"+std::to_string(sort_partial_batches)+
        ",\"sort_partial_instructions\":"+std::to_string(sort_partial_instructions);
}
}
