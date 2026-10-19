#include "native_weather.hpp"
#include "fp_registers.hpp"
#include "sse_kernel.hpp"
#include "integer_value.hpp"
#include "uv_clip.hpp"
#include "work_samples.hpp"
#include <cfenv>
#include <cerrno>
#include <cstring>
#pragma STDC FENV_ACCESS ON

namespace wa2::aot {namespace {
struct WeatherDeclined {wr::U32 address{};unsigned reason{};};
struct WeatherYield {};
struct WeatherEnvironment {
    std::fenv_t saved;int error{errno};
    WeatherEnvironment(){if(std::feholdexcept(&saved))throw WeatherDeclined{};
        if(std::fesetround(FE_TONEAREST)){std::fesetenv(&saved);errno=error;throw WeatherDeclined{};}}
    ~WeatherEnvironment(){std::fesetenv(&saved);errno=error;}
};
bool weather_single(fp::Extended value,float& result){
    if(value.zero()){result=std::bit_cast<float>(std::uint32_t(value.sign())<<31);return true;}
    const int exponent=int(value.exponent())-16383;
    if(exponent < -126 || exponent>127 || value.unsupported() || (value.significand&0xffffffffffull))return false;
    result=std::bit_cast<float>((std::uint32_t(value.sign())<<31)|(std::uint32_t(exponent+127)<<23)|std::uint32_t((value.significand>>40)&0x7fffff));return true;
}
fp::Extended weather_extended(float value){
    const auto bits=std::bit_cast<std::uint32_t>(value);const auto exponent=(bits>>23)&255;
    if(!exponent)return {0,std::uint16_t(bits>>31?0x8000:0)};
    return {std::uint64_t((bits&0x7fffff)|0x800000)<<40,std::uint16_t((bits>>31?0x8000:0)|(exponent-127+16383))};
}
template<unsigned Digit>bool weather_binary(fp::Extended first,fp::Extended second,fp::Environment& environment,fp::Extended& output){
    float left{},right{};if(!weather_single(first,left) || !weather_single(second,right))return false;
    constexpr auto operation=Digit==5?4:Digit==7?6:Digit;if constexpr(Digit==5 || Digit==7)std::swap(left,right);
    const auto a=std::bit_cast<std::uint32_t>(left),b=std::bit_cast<std::uint32_t>(right);
    const double x=left,y=right;double exact{},comparison{};float result{};
    if constexpr(operation==0 || operation==4){
        if((a&0x7fffffff) && (b&0x7fffffff) && std::abs(int((a>>23)&255)-int((b>>23)&255))>29)return false;
        if constexpr(operation==0){exact=x+y;result=left+right;}else{exact=x-y;result=left-right;}comparison=double(result);
    }else if constexpr(operation==1){exact=x*y;result=left*right;comparison=double(result);}
    else if constexpr(operation==6){if(!(b&0x7fffffff))return false;result=left/right;exact=x;comparison=double(result)*y;}
    else return false;
    const auto bits=std::bit_cast<std::uint32_t>(result),exponent=bits&0x7f800000u;
    if(exponent==0x7f800000u || (!exponent && (bits&0x7fffffff)) || (!(bits&0x7fffffff) && exact!=0))return false;
    environment.status&=~fp::RoundUp;
    const auto expected=std::bit_cast<std::uint64_t>(exact)&0x7fffffffffffffffull,rounded=std::bit_cast<std::uint64_t>(comparison)&0x7fffffffffffffffull;
    if(expected!=rounded){environment.raise(fp::Inexact);if(rounded>expected)environment.raise(fp::RoundUp);}
    output=weather_extended(result);return true;
}
class WeatherFrame {
    const wr::Memory::NativeIndex* const* pages_;
    std::array<std::uint8_t,16> read_scratch_{};
    unsigned iterations_{};
public:
    static constexpr wr::U32 SpriteBase=0xab3fb8+81*256,WeatherBase=0x522000;
    static constexpr unsigned SpriteBytes=800*256,WeatherBytes=0x5000,StackBytes=512;
    wr::U32 base;
    bool native_math{};
    std::array<std::uint8_t,StackBytes> stack;
    std::array<std::uint8_t,WeatherBytes> weather;
    std::vector<std::uint8_t>& sprites;
    WeatherFrame(wr::U32 sp,const wr::Memory::ExecutionScope& lock,std::vector<std::uint8_t>& bytes):
        pages_(lock.native_pages()),base(sp-256),sprites(bytes){}
    void iteration(){if(++iterations_>2048)throw WeatherDeclined{};}
    std::uint8_t* writable(wr::U32 at,unsigned size){
        auto offset=std::uint64_t(at)-base;
        if(offset<=stack.size() && size<=stack.size()-offset)return stack.data()+offset;
        offset=std::uint64_t(at)-SpriteBase;
        if(offset<=sprites.size() && size<=sprites.size()-offset)return sprites.data()+offset;
        offset=std::uint64_t(at)-WeatherBase;
        if(offset<=weather.size() && size<=weather.size()-offset)return weather.data()+offset;
        throw WeatherDeclined{at,1};
    }
    const std::uint8_t* readable(wr::U32 at,unsigned size){
        const auto in=[](wr::U32 at,unsigned size,wr::U32 base,unsigned length){const auto off=std::uint64_t(at)-base;return off<=length && size<=length-off;};
        if(in(at,size,base,stack.size()) || in(at,size,SpriteBase,sprites.size()) || in(at,size,WeatherBase,weather.size()))return writable(at,size);
        const auto overlaps=[&](wr::U32 begin,unsigned length){return std::uint64_t(at)<std::uint64_t(begin)+length && std::uint64_t(begin)<std::uint64_t(at)+size;};
        if(overlaps(base,stack.size()) || overlaps(SpriteBase,sprites.size()) || overlaps(WeatherBase,weather.size()))throw WeatherDeclined{};
        if(!size || size>read_scratch_.size() || std::uint64_t(at)+size>0x100000000ull)throw WeatherDeclined{};
        for(unsigned done=0;done<size;){
            const auto address=at+done,offset=address&4095;const auto* index=pages_[address>>22];
            if(!index)throw WeatherDeclined{};const auto& page=(*index)[(address>>12)&1023];
            const auto amount=std::min(size-done,4096-offset);
            if(!page.data || !(page.permissions&wr::Memory::Read) || offset+amount>page.length)throw WeatherDeclined{};
            if(!done && amount==size)return page.data+offset;
            std::memcpy(read_scratch_.data()+done,page.data+offset,amount);done+=amount;
        }
        return read_scratch_.data();
    }
    template<unsigned Width>wr::U32 load(wr::U32 at){return wr::U32(fp::detail::read_word(readable(at,Width/8),Width/8));}
    template<unsigned Width>void store(wr::U32 at,wr::U32 value){fp::detail::write_word(writable(at,Width/8),value,Width/8);}
};
class WeatherFloating {
    wr::Cpu& cpu_;WeatherFrame& frame_;fp::detail::RegisterStack stack_;
    std::uint64_t instruction_,data_;std::uint16_t opcode_;bool dirty_{};
    template<unsigned Opcode,unsigned Encoding,bool Memory>bool arithmetic(void* operand){
        constexpr auto digit=Memory?Encoding:(Encoding>>3)&7,index=Encoding&7;
        constexpr bool binary=digit==0 || digit==1 || digit>=4;
        fp::Extended right{},result{};unsigned destination{};bool pop{};
        if constexpr(Memory && (Opcode==216 || Opcode==220) && binary){
            if(!stack_.present(0))return false;fp::Environment conversion;
            right=fp::from_binary(fp::detail::read_word(operand,Opcode==216?4:8),Opcode==216?32:64,conversion);
            if(!weather_binary<digit>(stack_.raw(0),right,stack_.environment,result))return false;
        }else if constexpr(!Memory && Opcode==216 && binary){
            if(!stack_.present(0) || !stack_.present(index))return false;
            if(!weather_binary<digit>(stack_.raw(0),stack_.raw(index),stack_.environment,result))return false;
        }else if constexpr(!Memory && Opcode==222 && binary){
            if(!stack_.present(0) || !stack_.present(index))return false;
            if(!weather_binary<(digit>=4?digit^1:digit)>(stack_.raw(index),stack_.raw(0),stack_.environment,result))return false;
            destination=index;pop=true;
        }else return false;
        stack_.set(destination,result);if(pop)stack_.pop();return true;
    }
public:
    WeatherFloating(wr::Cpu& cpu,WeatherFrame& frame):cpu_(cpu),frame_(frame),instruction_(cpu.fp.get64(8)),data_(cpu.fp.get64(16)),opcode_(cpu.fp.get16(6)){stack_.load(cpu.fp);}
    std::uint16_t word(unsigned offset)const{return offset==0?stack_.environment.control:stack_.environment.status;}
    template<unsigned Opcode,unsigned Encoding,bool Memory>
    void execute(void* operand,unsigned opcode,bool control=false,bool integer_flags=false,wr::U32 data=0,bool has_memory=false){
        if(integer_flags)throw WeatherDeclined{};
        stack_.begin();bool native{};
        if(frame_.native_math && !(stack_.environment.control&0xf00))native=arithmetic<Opcode,Encoding,Memory>(operand);
        if(!native)fp::x87_operation<Opcode,Encoding,Memory>(stack_,operand);
        else if(current_work_samples)++current_work_samples->weather_native_operations;
        stack_.finish();dirty_=true;
        if((stack_.environment.control&0x3f)!=0x3f)throw WeatherDeclined{};
        if(!control){instruction_=cpu_.eip;opcode_=std::uint16_t(opcode&0x7ff);if(has_memory)data_=data;}
    }
    template<unsigned Opcode,unsigned Encoding,bool Memory>
    void memory(wr::U32 address,unsigned size,bool store,unsigned opcode,bool control=false){
        if(!size || size>10)throw WeatherDeclined{};
        auto* operand=store?frame_.writable(address,size):const_cast<std::uint8_t*>(frame_.readable(address,size));
        execute<Opcode,Encoding,Memory>(operand,opcode,control,false,address,true);
    }
    void commit(){if(dirty_){stack_.save(cpu_.fp);cpu_.fp.put64(8,instruction_);cpu_.fp.put64(16,data_);cpu_.fp.put16(6,opcode_);}}
};
void weather_push(wr::Cpu& s,WeatherFrame& frame,wr::U32 value,unsigned width=32){
    const auto at=s.r[wr::ESP]-width/8;fp::detail::write_word(frame.writable(at,width/8),value,width/8);s.r[wr::ESP]=at;
}
wr::U32 weather_pop(wr::Cpu& s,WeatherFrame& frame,unsigned width=32){
    const auto at=s.r[wr::ESP];const auto value=wr::U32(fp::detail::read_word(frame.readable(at,width/8),width/8));s.r[wr::ESP]+=width/8;return value;
}
void weather_string(wr::Cpu& s,WeatherFrame& frame,unsigned kind,unsigned width,unsigned address_width=32,unsigned repeat=0,unsigned segment=0){
    if(kind>1 || address_width!=32 || segment || (s.flags&wr::DF) || (width!=8 && width!=16 && width!=32))throw WeatherDeclined{};
    const auto step=width/8,count=repeat?s.r[wr::ECX]:1u;
    if(count>WeatherFrame::SpriteBytes/step)throw WeatherDeclined{};const auto bytes=count*step;
    if(bytes){
        auto* destination=frame.writable(s.r[wr::EDI],bytes);
        if(kind==1){
            const auto word=s.r[wr::EAX]&wr::mask(width);
            if(!word || width==8)std::memset(destination,int(word&255),bytes);
            else for(unsigned at=0;at<bytes;at+=step)fp::detail::write_word(destination+at,word,step);
        }else for(unsigned at=0;at<bytes;at+=step){
            const auto value=fp::detail::read_word(frame.readable(s.r[wr::ESI]+at,step),step);fp::detail::write_word(destination+at,value,step);
        }
    }
    if(kind==0)s.r[wr::ESI]+=bytes;s.r[wr::EDI]+=bytes;if(repeat)s.r[wr::ECX]=0;
}
void weather_vector_clear(wr::Cpu& s,WeatherFrame& frame){
    const auto count=s.r[wr::ECX];if(!count || count>8192 || (s.r[wr::EDI]&15) || s.fp.get64(160) || s.fp.get64(168))throw WeatherDeclined{};
    std::memset(frame.writable(s.r[wr::EDI],count*128),0,count*128);s.r[wr::EDI]+=count*128;s.r[wr::ECX]=wr::incdec(s,1,32,true);
}
template<auto Kernel>void weather_sse_memory(wr::Cpu& s,WeatherFrame& frame,wr::U32 at,unsigned size,bool store,unsigned alignment,bool integer_flags=false){
    if((alignment && at%alignment) || !size || size>16)throw WeatherDeclined{};
    std::array<std::uint8_t,16> operand{};auto* output=store?frame.writable(at,size):nullptr;
    if(!store)std::memcpy(operand.data(),frame.readable(at,size),size);
    fp::sse_execute<Kernel>(s,operand.data(),integer_flags);if(store)std::memcpy(output,operand.data(),size);
}
template<auto Kernel>wr::U32 weather_sse_to_i32(wr::Cpu& s,WeatherFrame& frame,wr::U32 at){
    std::array<std::uint8_t,16> operand{};std::memcpy(operand.data(),frame.readable(at,8),8);
    const auto bits=fp::detail::read_word(operand.data(),8);const auto exponent=bits&0x7ff0000000000000ull;
    if(frame.native_math && exponent!=0x7ff0000000000000ull && (exponent || !(bits&0x7fffffffffffffffull))){
        const auto value=std::bit_cast<double>(bits);
        if(value>=-2147483648. && value<2147483648.){
            const auto integer=std::int32_t(value);if(double(integer)!=value)s.fp.put32(24,s.fp.get32(24)|0x20);
            s.fp.put32(28,0xffff);s.fp.clear_x87_padding();return std::bit_cast<std::uint32_t>(integer);
        }
    }
    fp::sse_execute<Kernel>(s,operand.data());return wr::U32(fp::detail::read_word(operand.data(),4));
}
bool weather_color_direct(wr::Cpu& s,WeatherFrame& frame){
    // Cached 0x43b290..0x43b3be: one corner's signed image-mode test,
    // three channel expansions and ARGB packing. Retain its scratch words,
    // saved registers, volatile results, final ADD flags and source return.
    if(current_work_samples)++current_work_samples->weather_color_attempts;
    const auto sp=s.r[wr::ESP],index=frame.load<32>(sp+4),corner=frame.load<32>(sp+8);
    auto red=frame.load<32>(sp+16),green=frame.load<32>(sp+20),blue=frame.load<32>(sp+24);
    if(index>=2048 || corner>=4 || red>255 || green>255 || blue>255)return false;
    const auto returning=frame.load<32>(sp);const auto alpha=std::bit_cast<std::int32_t>(frame.load<32>(sp+12));
    const auto mode=std::bit_cast<std::int16_t>(std::uint16_t(frame.load<16>(0xab4002+index*256)));
    unsigned red_extra{},green_extra{},blue_extra{};
    if(mode>=0){
        const auto expand=[](unsigned& channel){const auto original=channel;channel=original>=128?255:original*2;return original>=128?original*2-256:0u;};
        red_extra=expand(red);green_extra=expand(green);blue_extra=expand(blue);
    }
    frame.store<32>(sp-12,s.r[wr::EBX]);frame.store<32>(sp-16,s.r[wr::EBP]);frame.store<32>(sp-20,s.r[wr::ESI]);
    frame.store<32>(sp-8,green_extra);frame.store<32>(sp-4,blue_extra);
    if(mode>=0){frame.store<32>(sp+16,red);frame.store<32>(sp+20,green);}
    const auto offset=index*256+corner*4;
    frame.store<32>(0xab400b+offset,0xff000000u|(red_extra<<16)|(green_extra<<8)|blue_extra);
    const auto color=(unsigned(std::clamp(alpha,0,255))<<24)|(red<<16)|(green<<8)|blue;
    frame.store<32>(0xab401b+offset,color);s.r[wr::EAX]=color;s.r[wr::ECX]=green;s.r[wr::EDX]=blue;
    (void)wr::add(s,sp-8,8,32);s.r[wr::ESP]=sp+4;s.eip=returning;
    if(current_work_samples)++current_work_samples->weather_color_completed;return true;
}
#include "../generated/shared-aot/weather_batch.inc"
bool writable_frame(const wr::Memory::ExecutionScope& lock,wr::U32 at,unsigned size){
    if(std::uint64_t(at)+size>0x100000000ull)return false;const auto pages=lock.native_pages();
    for(unsigned done=0;done<size;){
        const auto address=at+done,offset=address&4095,amount=std::min(size-done,4096-offset);const auto* index=pages[address>>22];
        if(!index)return false;const auto& page=(*index)[(address>>12)&1023];
        if(!page.data || (page.permissions&7)!=3 || offset+amount>page.length)return false;done+=amount;
    }
    return true;
}
}
#if WA2_AOT_DIAGNOSTICS
bool native_weather_color_snapshot(wr::Cpu& cpu,wr::Memory& memory){
    if(cpu.eip!=0x43b290 || cpu.r[wr::ESP]<256)return false;auto lock=memory.execution_scope();
    if(!writable_frame(lock,cpu.r[wr::ESP]-256,WeatherFrame::StackBytes) ||
       !writable_frame(lock,WeatherFrame::SpriteBase,WeatherFrame::SpriteBytes))return false;
    try{
        static thread_local std::vector<std::uint8_t> bytes(WeatherFrame::SpriteBytes);
        WeatherFrame frame(cpu.r[wr::ESP],lock,bytes);memory.copy_out(frame.base,frame.stack);memory.copy_out(frame.SpriteBase,frame.sprites);
        auto result=cpu;if(!weather_color_direct(result,frame))return false;
        memory.copy_in(frame.base,frame.stack);memory.copy_in(frame.SpriteBase,frame.sprites);cpu=result;return true;
    }catch(const WeatherDeclined&){return false;}catch(const wr::GuestFault&){return false;}
}
#endif
bool native_weather_population(wr::Memory& memory){
    // Evaluate before serializing register caches. A failed speculative read
    // must leave the original static body's memory-fault prefix unchanged.
    const int error=errno;
    try{const auto count=memory.load(0x5267d4,32);return count>=64 && count<=400;}
    catch(const wr::GuestFault&){errno=error;return false;}
}
bool native_weather_batch(wr::Cpu& cpu,wr::Memory& memory){
#if WA2_AOT_DIAGNOSTICS
    if(!native_render_math_enabled)return false;
#endif
    auto* work=current_work_samples;if(work)++work->weather_batch_attempts;
    struct Bound {const wr::Cpu* cpu{};const wr::Memory* memory{};wr::U32 stack{};unsigned remaining{};};
    static thread_local Bound bound;
    if(cpu.eip==0x4088cf || bound.cpu!=&cpu || bound.memory!=&memory || bound.stack!=cpu.r[wr::ESP])
        bound={&cpu,&memory,cpu.r[wr::ESP],8};
    if(!bound.remaining)return false;
    if((cpu.eip!=0x4088cf && cpu.eip!=0x408920) || cpu.r[wr::ESP]<256 ||
       (cpu.fp.get16(0)&0x3f)!=0x3f || cpu.fp.bytes[4] || (cpu.flags&wr::DF) ||
       (cpu.fp.get32(24)&0xffff1f80u)!=0x1f80u || cpu.r[wr::EBP]!=1)return false;
    auto lock=memory.execution_scope();
    try{
        // Small populations are faster in the ordinary register-cached static
        // body. Decide once per update so its continuation checks stay cheap.
        const auto count=memory.load(0x5267d4,32);
        if(count<64 || count>400){bound.remaining=0;return false;}
        const auto overlap=[](std::uint64_t a,unsigned n,std::uint64_t b,unsigned k){return a<b+k && b<a+n;};
        if(overlap(cpu.r[wr::ESP]-256,WeatherFrame::StackBytes,WeatherFrame::SpriteBase,WeatherFrame::SpriteBytes) ||
           overlap(cpu.r[wr::ESP]-256,WeatherFrame::StackBytes,WeatherFrame::WeatherBase,WeatherFrame::WeatherBytes))return false;
        if(!writable_frame(lock,cpu.r[wr::ESP]-256,WeatherFrame::StackBytes) ||
           !writable_frame(lock,WeatherFrame::SpriteBase,WeatherFrame::SpriteBytes) ||
           !writable_frame(lock,WeatherFrame::WeatherBase,WeatherFrame::WeatherBytes))return false;
        static thread_local std::vector<std::uint8_t> bytes(WeatherFrame::SpriteBytes);
        const auto now=[](){return std::chrono::steady_clock::now();};auto read_start=work?now():std::chrono::steady_clock::time_point{};
        const auto ns=[](auto value){return std::chrono::duration_cast<std::chrono::nanoseconds>(value).count();};
        WeatherFrame frame(cpu.r[wr::ESP],lock,bytes);memory.copy_out(frame.base,frame.stack);
        memory.copy_out(frame.SpriteBase,frame.sprites);memory.copy_out(frame.WeatherBase,frame.weather);
        const auto calculate_start=work?now():std::chrono::steady_clock::time_point{};
        auto result=cpu;WeatherEnvironment environment;frame.native_math=true;WeatherFloating floating(result,frame);bool yielded{};
        try{weather_4088cf(result,frame,floating);}catch(const WeatherYield&){yielded=true;}
        catch(const WeatherDeclined& failure){if(work){work->weather_decline_pc=result.eip;work->weather_decline_address=failure.address;work->weather_decline_reason=failure.reason;}throw;}
        floating.commit();const auto write_start=work?now():std::chrono::steady_clock::time_point{};
        memory.copy_in(frame.base,frame.stack);memory.copy_in(frame.SpriteBase,frame.sprites);
        memory.copy_in(frame.WeatherBase,frame.weather);cpu=result;
        --bound.remaining;if(work){++work->weather_batch_completed;if(yielded)++work->weather_batch_yields;
            work->weather_read_ns+=ns(calculate_start-read_start);work->weather_calculate_ns+=ns(write_start-calculate_start);work->weather_write_ns+=ns(now()-write_start);}return true;
    }catch(const WeatherDeclined&){bound.remaining=0;return false;}catch(const wr::GuestFault&){bound.remaining=0;return false;}
}
}
