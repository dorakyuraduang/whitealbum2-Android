#include "uv_clip.hpp"
#include "extended80.hpp"
#include <winrecomp/runtime.hpp>
#include <bit>
#include <cstring>

namespace wa2::aot {
#if WA2_AOT_DIAGNOSTICS
thread_local bool native_render_math_enabled=true;
#endif
namespace {
struct Declined {};
// Local values for one complete clipping loop. The source FP serialization is
// read once and committed once, rather than rotating/storing it after every
// arithmetic operation. Dead physical register payloads remain exact too.
class Values {
    std::array<fp::Extended,8> physical;
    unsigned top;std::uint8_t tags;
    unsigned slot(unsigned index)const{return (top+index)&7;}
    void update_top(){environment.status=std::uint16_t((environment.status&~0x3800)|(top<<11));}
public:
    fp::Environment environment;
    explicit Values(const wr::FpState& state):top(state.top()),tags(state.bytes[4]),environment{state.get16(0),state.get16(2)}{
        for(unsigned n=0;n<8;++n)physical[slot(n)]={state.get64(32+n*16),state.get16(40+n*16)};
    }
    fp::Extended get(unsigned index)const{if(!(tags&(1u<<slot(index))))throw Declined{};return physical[slot(index)];}
    void set(unsigned index,fp::Extended value){physical[slot(index)]=value;tags|=std::uint8_t(1u<<slot(index));}
    void push(fp::Extended value){const auto next=(top+7)&7;if(tags&(1u<<next))throw Declined{};top=next;update_top();set(0,value);}
    void pop(){if(!(tags&(1u<<top)))throw Declined{};tags&=std::uint8_t(~(1u<<top));top=(top+1)&7;update_top();}
    void load(std::uint32_t value){push(fp::from_binary(value,32,environment));}
    void load64(std::uint64_t value){push(fp::from_binary(value,64,environment));}
    void duplicate(unsigned index){environment.status&=~fp::RoundUp;push(get(index));}
    void exchange(unsigned index){environment.status&=~fp::RoundUp;const auto first=get(0),second=get(index);set(0,second);set(index,first);}
    void drop(){environment.status&=~fp::RoundUp;pop();}
    std::uint32_t store(bool popping=false){const auto value=std::uint32_t(fp::to_binary(get(0),32,environment));if(popping)pop();return value;}
    void subtract_pair(bool reverse){const auto first=get(0),second=get(2);set(2,reverse?fp::subtract(first,second,environment):fp::subtract(second,first,environment));pop();}
    void divide(unsigned index){set(0,fp::divide(get(0),get(index),environment));}
    void multiply(unsigned index){set(0,fp::multiply(get(0),get(index),environment));}
    void add(unsigned index){set(0,fp::add(get(0),get(index),environment));}
    void add_pop(unsigned index){set(index,fp::add(get(index),get(0),environment));pop();}
    fp::Extended operand(std::uint32_t word){
        const auto exponent=word&0x7f800000u;
        if(exponent==0x7f800000u || (!exponent && (word&0x7fffffu)))throw Declined{};
        auto conversion=environment;return fp::from_binary(word,32,conversion);
    }
    void multiply_word(std::uint32_t word){const auto right=operand(word);set(0,fp::multiply(get(0),right,environment));}
    void multiply_double(std::uint64_t word){
        const auto exponent=word&0x7ff0000000000000ull;
        if(exponent==0x7ff0000000000000ull || (!exponent && (word&0xfffffffffffffull)))throw Declined{};
        auto conversion=environment;const auto right=fp::from_binary(word,64,conversion);set(0,fp::multiply(get(0),right,environment));
    }
    void add_word(std::uint32_t word){const auto right=operand(word);set(0,fp::add(get(0),right,environment));}
    void subtract_word(std::uint32_t word){const auto right=operand(word);set(0,fp::subtract(get(0),right,environment));}
    std::uint32_t integer(){const auto value=std::uint32_t(fp::to_integer(get(0),32,environment));pop();return value;}
    void control(std::uint16_t word){environment.control=std::uint16_t((word&0x1f3f)|0x40);
        if(environment.status&~environment.control&0x3f)environment.status|=0x8080;else environment.status&=~0x8080;}
    void compare(unsigned index){const auto result=fp::compare(get(0),get(index),environment,true);environment.status&=~0x4700;
        environment.status|=std::uint16_t(result==2?0x4500:result<0?0x100:result==0?0x4000:0);}
    void commit(wr::FpState& state){for(unsigned n=0;n<8;++n){const auto value=physical[slot(n)];state.put64(32+n*16,value.significand);state.put16(40+n*16,value.sign_exponent);}
        state.clear_x87_padding();state.bytes[4]=tags;state.put16(0,environment.control);state.put16(2,environment.status);state.put32(28,0xffff);}
};
bool clip(wr::Cpu& cpu,wr::Memory& memory,bool rows){
#if WA2_AOT_DIAGNOSTICS
    if(!native_render_math_enabled)return false;
#endif
    const auto begin=rows?0x43cd37u:0x43cc4du,end=rows?0x43cdf0u:0x43cd06u;
    const auto count=cpu.r[rows?wr::ESI:wr::EBX],first=cpu.r[wr::ECX],sp=cpu.r[wr::ESP];
    if(cpu.eip!=begin || (cpu.fp.get16(0)&0x3f)!=0x3f || count>32 || first>=count || sp>0xfffffc3fu)return false;
    if(std::uint64_t(sp)<0x43df70 && std::uint64_t(sp)+0x3c0>0x43b7a0)return false;
    auto scope=memory.execution_scope();
    alignas(16) std::array<std::uint8_t,0x3c0> scratch;
    try{
        memory.check(sp,scratch.size(),wr::Memory::Read|wr::Memory::Write);
        for(std::uint64_t at=sp;at<std::uint64_t(sp)+scratch.size();at=(at/4096+1)*4096)if(memory.permissions(wr::U32(at))&wr::Memory::Execute)return false;
        memory.copy_out(sp,scratch);
    }catch(const wr::GuestFault&){return false;}
    auto word=[&](unsigned at){std::uint32_t value;std::memcpy(&value,scratch.data()+at,4);return value;};
    auto put=[&](unsigned at,std::uint32_t value){std::memcpy(scratch.data()+at,&value,4);};
    auto eax=cpu.r[wr::EAX];Values values(cpu.fp);auto& env=values.environment;
    const auto status=[&]{eax=(eax&0xffff0000u)|env.status;};
    const auto parity=[&]{return (std::popcount((eax>>8)&5u)&1)==0;};
    try{for(auto index=first;index<count;++index){
        env.status&=~fp::RoundUp;values.push(fp::from_integer(std::bit_cast<std::int32_t>(word(0x30))));put(0x10,values.store(true));
        if(rows)values.load(word(0x68));else values.duplicate(3);
        values.load(word(0x10));values.duplicate(0);values.subtract_pair(false);values.exchange(1);values.divide(rows?2:4);
        put(0x10,values.store(true));values.load(word(0x10));
        const auto a=(rows?0x340u:0x268u)+index*4,b=(rows?0x2b0u:0x2f8u)+index*4;
        put(a,values.store());if(rows)values.load(word(0x6c));else values.duplicate(6);
        values.subtract_pair(true);values.exchange(1);values.divide(rows?2:4);
        put(0x10,values.store(true));values.load(word(0x10));put(b,values.store());values.exchange(1);
        values.compare(rows?4:2);status();
        if(parity()){values.drop();values.exchange(rows?3:1);put(0x1c,values.store());values.exchange(rows?3:1);}
        else{values.compare(3);status();if((eax>>8)&0x41){values.drop();values.exchange(2);put(0x1c,values.store());values.exchange(2);}else put(0x1c,values.store(true));}
        values.load(word(0x1c));put(a,values.store(true));
        values.compare(rows?3:1);status();
        if(parity()){values.drop();if(rows)values.exchange(2);put(0x1c,values.store());if(rows)values.exchange(2);}
        else{values.compare(2);status();if((eax>>8)&0x41){values.drop();values.exchange(1);put(0x1c,values.store());values.exchange(1);}else put(0x1c,values.store(true));}
        values.load(word(0x1c));put(0x30,word(0x30)+cpu.r[wr::EDX]);put(b,values.store(true));
    }}catch(const Declined&){return false;}
    auto result=cpu.fp;values.commit(result);result.put64(8,rows?0x43cde0:0x43ccf6);
    result.put64(16,sp+(rows?0x2b0:0x2f8)+(count-1)*4);result.put16(6,412);
    // Permission and alias admission happened before any state changed.
    memory.copy_in(sp,scratch);cpu.fp=result;cpu.r[wr::EAX]=eax;cpu.r[wr::ECX]=count;
    wr::sub(cpu,count,count,32);cpu.eip=end;return true;
}
}
bool native_uv_columns(wr::Cpu& cpu,wr::Memory& memory){return clip(cpu,memory,false);}
bool native_uv_rows(wr::Cpu& cpu,wr::Memory& memory){return clip(cpu,memory,true);}
bool native_vertex_interpolation(wr::Cpu& cpu,wr::Memory& memory){
#if WA2_AOT_DIAGNOSTICS
    if(!native_render_math_enabled)return false;
#endif
    constexpr wr::U32 begin=0x43d3cd,end=0x43d59f;constexpr unsigned FrameBytes=0x16a4;
    const auto count=cpu.r[wr::EBX],first=cpu.r[wr::EDX],offset=cpu.r[wr::ECX],sp=cpu.r[wr::ESP];
    if(cpu.eip!=begin || (cpu.fp.get16(0)&0x3f)!=0x3f || count>32 || first>=count ||
       (offset&3) || std::uint64_t(offset)+std::uint64_t(count-first)*4>0x500 || std::uint64_t(sp)+FrameBytes>0x100000000ull)return false;
    if(std::uint64_t(sp)<0x43df70 && std::uint64_t(sp)+FrameBytes>0x43b7a0)return false;
    auto scope=memory.execution_scope();alignas(16) std::array<std::uint8_t,FrameBytes> scratch;
    try{memory.check(sp,scratch.size(),wr::Memory::Read|wr::Memory::Write);
        for(std::uint64_t at=sp;at<std::uint64_t(sp)+scratch.size();at=(at/4096+1)*4096)if(memory.permissions(wr::U32(at))&wr::Memory::Execute)return false;
        memory.copy_out(sp,scratch);
    }catch(const wr::GuestFault&){return false;}
    auto word=[&](unsigned at){std::uint32_t value;std::memcpy(&value,scratch.data()+at,4);return value;};
    auto put=[&](unsigned at,std::uint32_t value){std::memcpy(scratch.data()+at,&value,4);};
    auto save_control=[&](std::uint16_t control){put(0x14,(word(0x14)&0xffff0000u)|control);};
    auto double_word=[&](unsigned at){std::uint64_t value;std::memcpy(&value,scratch.data()+at,8);return value;};
    Values values(cpu.fp);auto& env=values.environment;auto byte_offset=offset;std::uint32_t eax{};
    struct Channel {unsigned difference,base,destination;};
    constexpr Channel channels[]={{0x1b8,0x4c,0x38b},{0x150,0x48,0x38a},{0x1a8,0x2c,0x389},
        {0x158,0x28,0x388},{0xa8,0x7c,0x812},{0xa0,0x18,0x811},{0x198,0x10,0x810}};
    try{for(auto index=first;index<count;++index){
        values.load(word(0x14));values.duplicate(0);values.multiply(5);save_control(env.control);values.divide(4);
        const auto truncated=std::uint32_t(env.control)|0xc00u;put(0x20,values.store(true));
        values.load(word(0x20));values.duplicate(0);values.multiply(4);put(0x20,truncated);values.add(3);
        put(0x1120+byte_offset,values.store(true));
        values.load64(double_word(0x178));values.multiply(1);values.add_word(word(0x30));put(0xc98+byte_offset,values.store(true));
        for(unsigned n=0;n<std::size(channels);++n){const auto& channel=channels[n];
            if(n<6){values.load64(double_word(channel.difference));values.multiply(1);}else values.multiply_double(double_word(channel.difference));
            save_control(env.control);values.add_word(word(channel.base));put(0x20,std::uint32_t(env.control)|0xc00u);
            values.control(std::uint16_t(word(0x20)));put(0x20,values.integer());eax=word(0x20)&255;
            scratch[channel.destination+byte_offset]=std::uint8_t(eax);values.control(std::uint16_t(word(0x14)));
        }
        byte_offset+=4;const auto next=index+1;values.load(word(0x2f4+next*4));values.subtract_word(word(0x264+next*4));
        values.add_pop(1);put(0x14,values.store(true));
    }}catch(const Declined&){return false;}
    auto result=cpu.fp;values.commit(result);result.put64(8,0x43d595);result.put64(16,sp+0x14);result.put16(6,348);
    memory.copy_in(sp,scratch);cpu.fp=result;cpu.r[wr::EAX]=eax;cpu.r[wr::EDX]=count;cpu.r[wr::ECX]=byte_offset;
    wr::sub(cpu,count,count,32);cpu.eip=end;return true;
}
}
