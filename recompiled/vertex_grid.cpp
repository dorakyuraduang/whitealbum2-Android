#include "uv_clip.hpp"
#include "fp_registers.hpp"
#include "integer_value.hpp"
#include "work_samples.hpp"
#include "geometry_cache_detail.hpp"
#include "../engine/math_environment.hpp"
#include <cstring>
#pragma STDC FENV_ACCESS ON

namespace wa2::aot {namespace {
thread_local GeometryCacheCounts memo_counts;
// At most 256 grid entries (~3.4 MiB) and 128 small corner entries (~0.1 MiB).
// Lazy allocations are retained and reused; dynamic particle inputs evict
// old entries instead of growing memory across fast-forward sessions.
// Neither proved fragment reads or writes EBP. Retain each caller's EBP
// instead of allowing that unrelated value to defeat glyph reuse.
using GridMemo=detail::GeometryCache<0x16d4+8+256+12,4,64,0x16d4,0xdf,true>;
using SpriteMemo=detail::GeometryCache<0xa0+256+9,4,32,0xa0,0xdf,true>;
thread_local GridMemo grid_memo;
thread_local SpriteMemo sprite_memo;
bool memo_enabled(){
#if WA2_AOT_DIAGNOSTICS
    return native_geometry_cache_enabled;
#else
    return true;
#endif
}
struct GridDeclined {};
struct GeometryEnvironment {
    std::fenv_t saved;int error{errno};
    GeometryEnvironment(){
        if(std::feholdexcept(&saved))throw GridDeclined{};
        // Only finite normal/zero inputs and normal results are admitted.
        // Denormal flushing cannot affect an accepted step: a flushed
        // nonzero result rejects the transaction. Keep the caller's other
        // controls and avoid resetting the complete environment per sprite.
        if(std::fesetround(FE_TONEAREST)){
            std::fesetenv(&saved);errno=error;throw GridDeclined{};
        }
    }
    ~GeometryEnvironment(){std::fesetenv(&saved);errno=error;}
};
// One checked memory transaction for the complete vertex grid. Every source
// store targets this local frame; a rejected input commits neither CPU nor RAM.
class GridFrame {
public:
    static constexpr unsigned Size=0x16d4;
    alignas(16) std::array<std::uint8_t,Size> bytes;
    wr::U32 base{},resource{},sprite{};
    std::array<std::uint8_t,8> dimensions{};
    std::array<std::uint8_t,256> sprite_fields{};
    [[gnu::always_inline]] std::uint8_t* writable(wr::U32 address,unsigned size){
        const auto offset=std::uint64_t(address)-base;
        if(offset>Size || size>Size-offset)throw GridDeclined{};
        return bytes.data()+offset;
    }
    [[gnu::always_inline]] const std::uint8_t* readable(wr::U32 address,unsigned size){
        const auto offset=std::uint64_t(address)-base;
        if(offset<=Size && size<=Size-offset)return bytes.data()+offset;
        const auto dimension=std::uint64_t(address)-resource;
        if(dimension<=dimensions.size() && size<=dimensions.size()-dimension)return dimensions.data()+dimension;
        const auto field=std::uint64_t(address)-sprite;
        if(sprite && field<=sprite_fields.size() && size<=sprite_fields.size()-field)return sprite_fields.data()+field;
        throw GridDeclined{};
    }
    template<unsigned Width> [[gnu::always_inline]] wr::U32 load(wr::U32 address){
        static_assert(Width==8 || Width==16 || Width==32);
        return wr::U32(fp::detail::read_word(readable(address,Width/8),Width/8));
    }
    template<unsigned Width> [[gnu::always_inline]] void store(wr::U32 address,wr::U32 value){
        static_assert(Width==8 || Width==16 || Width==32);
        fp::detail::write_word(writable(address,Width/8),value,Width/8);
    }
};
class GridFloating {
    wr::Cpu& cpu_;GridFrame& frame_;fp::detail::RegisterStack stack_;
    std::uint64_t instruction_,data_;std::uint16_t opcode_;
public:
    GridFloating(wr::Cpu& cpu,GridFrame& frame):cpu_(cpu),frame_(frame),
        instruction_(cpu.fp.get64(8)),data_(cpu.fp.get64(16)),opcode_(cpu.fp.get16(6)){stack_.load(cpu.fp);}
    std::uint16_t word(unsigned offset)const{return offset==0?stack_.environment.control:stack_.environment.status;}
    template<unsigned Opcode,unsigned Encoding,bool Memory>
    void execute(void* operand,unsigned opcode,bool control=false,bool integer_flags=false,wr::U32 data=0,bool has_memory=false){
        stack_.begin();fp::x87_operation<Opcode,Encoding,Memory>(stack_,operand);stack_.finish();
        if(!control){instruction_=cpu_.eip;opcode_=std::uint16_t(opcode&0x7ff);if(has_memory)data_=data;}
        if(integer_flags)throw GridDeclined{};
        if((stack_.environment.control&0x3f)!=0x3f)throw GridDeclined{};
    }
    template<unsigned Opcode,unsigned Encoding,bool Memory>
    void memory(wr::U32 address,unsigned size,bool store,unsigned opcode,bool control=false){
        alignas(16) std::array<std::uint8_t,16> operand{};
        if(!size || size>10)throw GridDeclined{};
        auto* destination=store?frame_.writable(address,size):nullptr;
        if(!store)std::memcpy(operand.data(),frame_.readable(address,size),size);
        execute<Opcode,Encoding,Memory>(operand.data(),opcode,control,false,address,true);
        if(store)std::memcpy(destination,operand.data(),size);
    }
    void commit(){stack_.save(cpu_.fp);cpu_.fp.put64(8,instruction_);cpu_.fp.put64(16,data_);cpu_.fp.put16(6,opcode_);}
    template<unsigned Depth>std::uint16_t word_at(unsigned offset)const{return word(offset);}
    template<unsigned Depth,unsigned Opcode,unsigned Encoding,bool Memory,class... Args>void execute_at(Args&&... args){execute<Opcode,Encoding,Memory>(std::forward<Args>(args)...);}
    template<unsigned Depth,unsigned Opcode,unsigned Encoding,bool Memory,class... Args>void memory_at(Args&&... args){memory<Opcode,Encoding,Memory>(std::forward<Args>(args)...);}
    template<unsigned Depth>void finish_at(){}
};
#include "native_grid_float.inc"
// Four-corner geometry uses a small, disjoint local frame. The generated
// region has no host calls and commits all scratch words and FP observations.
class SpriteFrame {
public:
    static constexpr unsigned Size=0xa0;
    wr::U32 base{},sprite{},palette_address{};
    alignas(16) std::array<std::uint8_t,Size> bytes;
    std::array<std::uint8_t,256> fields;
    std::uint8_t palette{};
    [[gnu::always_inline]] std::uint8_t* writable(wr::U32 address,unsigned size){
        const auto offset=std::uint64_t(address)-base;
        if(offset>Size || size>Size-offset)throw GridDeclined{};
        return bytes.data()+offset;
    }
    [[gnu::always_inline]] const std::uint8_t* readable(wr::U32 address,unsigned size){
        const auto offset=std::uint64_t(address)-base;
        if(offset<=Size && size<=Size-offset)return bytes.data()+offset;
        const auto field=std::uint64_t(address)-sprite;
        if(field<=fields.size() && size<=fields.size()-field)return fields.data()+field;
        if(address==palette_address && size==1)return &palette;
        throw GridDeclined{};
    }
    template<unsigned Width> [[gnu::always_inline]] wr::U32 load(wr::U32 address){
        return wr::U32(fp::detail::read_word(readable(address,Width/8),Width/8));
    }
    template<unsigned Width> [[gnu::always_inline]] void store(wr::U32 address,wr::U32 value){
        fp::detail::write_word(writable(address,Width/8),value,Width/8);
    }
};
template<class Frame>struct TracingFrame:Frame {
    detail::GeometryTrace<Frame::Size> trace;
    explicit TracingFrame(const Frame& frame):Frame(frame){}
    std::uint8_t* writable(wr::U32 address,unsigned size){auto* value=Frame::writable(address,size);trace.write(unsigned(address-this->base),size);return value;}
    const std::uint8_t* readable(wr::U32 address,unsigned size){auto* value=Frame::readable(address,size);
        const auto offset=std::uint64_t(address)-this->base;if(offset<=Frame::Size && size<=Frame::Size-offset)trace.read(unsigned(offset),size);return value;}
    template<unsigned Width>wr::U32 load(wr::U32 address){return wr::U32(fp::detail::read_word(readable(address,Width/8),Width/8));}
    template<unsigned Width>void store(wr::U32 address,wr::U32 value){fp::detail::write_word(writable(address,Width/8),value,Width/8);}
};
template<class Frame>void calculate_sprite(wr::Cpu& __restrict s,Frame& __restrict scope){
    GridNativeFloating<true,true,false,true,Frame,0> floating(s,scope);
#include "../generated/shared-aot/sprite_geometry.inc"
    floating.commit();
}
class SnowFrame {
public:
    static constexpr unsigned Size=0x30;
    wr::U32 base{},particle{},curve_address{};
    std::array<std::uint8_t,Size> bytes;
    std::array<std::uint8_t,40> fields;
    std::array<std::uint8_t,8> speeds;
    std::array<std::uint8_t,2> curve;
    [[gnu::always_inline]] std::uint8_t* writable(wr::U32 at,unsigned size){
        const auto offset=std::uint64_t(at)-base,field=std::uint64_t(at)-particle;
        if(offset<=bytes.size() && size<=bytes.size()-offset)return bytes.data()+offset;
        if(field<=fields.size() && size<=fields.size()-field)return fields.data()+field;
        throw GridDeclined{};
    }
    [[gnu::always_inline]] const std::uint8_t* readable(wr::U32 at,unsigned size){
        if(at>=0x5267cc && at<=0x5267d4 && size<=0x5267d4-at)return speeds.data()+at-0x5267cc;
        if(at==curve_address && size==2)return curve.data();
        return writable(at,size);
    }
    template<unsigned Width> [[gnu::always_inline]] wr::U32 load(wr::U32 at){return wr::U32(fp::detail::read_word(readable(at,Width/8),Width/8));}
    template<unsigned Width> [[gnu::always_inline]] void store(wr::U32 at,wr::U32 value){fp::detail::write_word(writable(at,Width/8),value,Width/8);}
};
void calculate_snow(wr::Cpu& __restrict s,SnowFrame& __restrict scope){
    GridNativeFloating<true,true,false,true,SnowFrame,3> floating(s,scope);
#include "../generated/shared-aot/snow_motion.inc"
    floating.commit();
}
// Static source labels, not an instruction decoder. This region has no import,
// callback, indirect transfer or access outside the admitted frame/dimensions.
// Both objects are distinct local snapshots in native_vertex_grid. The
// generated region cannot call out or access another Cpu/host RAM object.
template<class Floating,class Frame> void calculate_grid(wr::Cpu& __restrict s,Frame& __restrict scope){
    Floating floating(s,scope);
#include "../generated/shared-aot/vertex_grid.inc"
    floating.commit();
}
}
GeometryCacheCounts geometry_cache_counts(){return memo_counts;}
#if WA2_AOT_DIAGNOSTICS
thread_local bool native_geometry_cache_enabled=true;
thread_local bool native_vertex_grid_enabled=true;
thread_local bool native_grid_float_enabled=true;
thread_local bool native_grid_registers_enabled=true;
thread_local bool native_sprite_geometry_enabled=true;
// Diagnostic entry to the same production arithmetic. Input rejection keeps
// the original state, exactly like rejection of a whole geometry transaction.
bool native_grid_binary_snapshot(wr::Cpu& cpu,std::uint32_t operand,unsigned digit){
    auto result=cpu;GridFrame frame;
    try{
        GeometryEnvironment environment;GridNativeFloating<> floating(result,frame);
        switch(digit){
        case 0:floating.execute<216,0,true>(&operand,0,false,false,0x70000000,true);break;
        case 1:floating.execute<216,1,true>(&operand,8,false,false,0x70000000,true);break;
        case 4:floating.execute<216,4,true>(&operand,32,false,false,0x70000000,true);break;
        case 6:floating.execute<216,6,true>(&operand,48,false,false,0x70000000,true);break;
        default:throw GridDeclined{};
        }
        floating.commit();cpu=result;return true;
    }catch(const GridDeclined&){return false;}
}
bool native_grid_stored_binary_snapshot(wr::Cpu& cpu,std::uint32_t operand,unsigned digit,std::uint32_t& output){
    auto result=cpu;GridFrame frame;std::uint32_t stored{};
    try{
        GeometryEnvironment environment;GridNativeFloating<true> floating(result,frame);
        switch(digit){
        case 0:floating.execute<216,0,true>(&operand,0,false,false,0x70000000,true);break;
        case 1:floating.execute<216,1,true>(&operand,8,false,false,0x70000000,true);break;
        case 4:floating.execute<216,4,true>(&operand,32,false,false,0x70000000,true);break;
        case 6:floating.execute<216,6,true>(&operand,48,false,false,0x70000000,true);break;
        default:throw GridDeclined{};
        }
        result.eip=0x43d595;floating.execute<217,3,true>(&stored,348,false,false,0x70000004,true);
        floating.commit();cpu=result;output=stored;return true;
    }catch(const GridDeclined&){return false;}
}
#endif
bool native_sprite_geometry(wr::Cpu& cpu,wr::Memory& memory){
#if WA2_AOT_DIAGNOSTICS
    if(!native_render_math_enabled || !native_sprite_geometry_enabled)return false;
#endif
    auto* sample=current_work_samples;if(sample)++sample->sprite_geometry_attempts;
    if(cpu.eip!=0x43bba8 || (cpu.fp.get16(0)&0xf3f)!=0x3f || cpu.fp.bytes[4])return false;
    SpriteFrame frame;frame.base=cpu.r[wr::ESP];frame.sprite=cpu.r[wr::ESI];
    const auto overlaps=[](std::uint64_t a,unsigned n,std::uint64_t b,unsigned k){return a<b+k && b<a+n;};
    if(std::uint64_t(frame.base)+frame.Size>0x100000000ull ||
       std::uint64_t(frame.sprite)+frame.fields.size()>0x100000000ull ||
       overlaps(frame.base,frame.Size,frame.sprite,frame.fields.size()))return false;
    auto lock=memory.execution_scope();
    try{
        memory.check(frame.base,frame.Size,wr::Memory::Read|wr::Memory::Write);
        for(std::uint64_t at=frame.base;at<std::uint64_t(frame.base)+frame.Size;at=(at/4096+1)*4096)
            if(memory.permissions(wr::U32(at))&wr::Memory::Execute)return false;
        memory.copy_out(frame.base,frame.bytes);memory.copy_out(frame.sprite,frame.fields);
        frame.palette_address=wr::U32(0x4c2293+std::int32_t(std::bit_cast<std::int8_t>(frame.fields[0x52]))*4);
        if(overlaps(frame.base,frame.Size,frame.palette_address,1))return false;
        frame.palette=std::uint8_t(memory.load(frame.palette_address,8));
        // Glyph templates are caller-local sprites. Continuously moving
        // global scene/particle sprites keep the allocation-free arithmetic.
        const bool memo=memo_enabled() && frame.sprite>frame.base && frame.sprite-frame.base<0x4000;
        SpriteMemo::Pending pending{};
        if(memo){
            std::array<std::uint8_t,SpriteFrame::Size+256+9> input;
            std::memcpy(input.data(),frame.bytes.data(),frame.bytes.size());std::memcpy(input.data()+frame.bytes.size(),frame.fields.data(),frame.fields.size());
            std::memcpy(input.data()+frame.bytes.size()+256,&frame.sprite,4);std::memcpy(input.data()+frame.bytes.size()+260,&frame.palette_address,4);input.back()=frame.palette;
            if(sprite_memo.lookup(cpu,input,frame.bytes,pending)){
                memory.copy_in(frame.base,frame.bytes);++memo_counts.sprite_hits;if(sample)++sample->sprite_geometry_completed;return true;
            }
            ++memo_counts.sprite_misses;
        }
        auto result=cpu;
        if(memo && pending.capture){++memo_counts.sprite_captures;TracingFrame<SpriteFrame> tracked(frame);
            {GeometryEnvironment environment;calculate_sprite(result,tracked);}
            frame.bytes=tracked.bytes;sprite_memo.commit(pending,result,frame.bytes,tracked.trace.inputs(SpriteFrame::Size+256+9),tracked.trace.outputs());
        }else{GeometryEnvironment environment;calculate_sprite(result,frame);}
        memory.copy_in(frame.base,frame.bytes);cpu=result;
        if(sample)++sample->sprite_geometry_completed;return true;
    }catch(const GridDeclined&){return false;}catch(const wr::GuestFault&){return false;}
}
bool native_snow_motion(wr::Cpu& cpu,wr::Memory& memory){
#if WA2_AOT_DIAGNOSTICS
    if(!native_render_math_enabled)return false;
#endif
    auto* sample=current_work_samples;if(sample)++sample->snow_motion_attempts;
    const auto top=cpu.fp.top();
    if(cpu.eip!=0x4089c5 || (cpu.fp.get16(0)&0xf3f)!=0x3f ||
       cpu.fp.bytes[4]!=std::rotl(std::uint8_t(7),int(top)) ||
       cpu.r[wr::ESI]>399*40 || cpu.r[wr::ESI]%40)return false;
    SnowFrame frame;frame.base=cpu.r[wr::ESP];frame.particle=0x522948+cpu.r[wr::ESI];
    const auto overlaps=[](std::uint64_t a,unsigned n,std::uint64_t b,unsigned k){return a<b+k && b<a+n;};
    if(std::uint64_t(frame.base)+frame.Size>0x100000000ull ||
       overlaps(frame.base,frame.Size,frame.particle,40) || overlaps(frame.base,frame.Size,0x5267cc,8))return false;
    auto lock=memory.execution_scope();
    try{
        for(auto [at,size]:{std::pair{frame.base,frame.Size},std::pair{frame.particle,40u}}){
            memory.check(at,size,wr::Memory::Read|wr::Memory::Write);
            for(std::uint64_t cursor=at;cursor<std::uint64_t(at)+size;cursor=(cursor/4096+1)*4096)
                if(memory.permissions(wr::U32(cursor))&wr::Memory::Execute)return false;
        }
        memory.copy_out(frame.base,frame.bytes);memory.copy_out(frame.particle,frame.fields);memory.copy_out(0x5267cc,frame.speeds);
        const auto phase=std::bit_cast<std::int16_t>(std::uint16_t(fp::detail::read_word(frame.fields.data()+8,2)));
        frame.curve_address=wr::U32(0x4a5b28+((int(phase)+64)%256)*2);
        if(overlaps(frame.base,frame.Size,frame.curve_address,2))return false;
        memory.copy_out(frame.curve_address,frame.curve);
        auto result=cpu;{GeometryEnvironment environment;calculate_snow(result,frame);}
        memory.copy_in(frame.base,frame.bytes);memory.copy_in(frame.particle,frame.fields);cpu=result;
        if(sample)++sample->snow_motion_completed;return true;
    }catch(const GridDeclined&){return false;}catch(const wr::GuestFault&){return false;}
}
bool native_vertex_grid(wr::Cpu& cpu,wr::Memory& memory){
#if WA2_AOT_DIAGNOSTICS
    if(!native_render_math_enabled || !native_vertex_grid_enabled)return false;
#endif
    auto* sample=current_work_samples;
    if(sample)++sample->vertex_grid_attempts;
    const bool clipping=cpu.eip==0x43cc17;
    if(sample && clipping)++sample->vertex_grid_clipping_attempts;
    if((!clipping && cpu.eip!=0x43ce95) || (cpu.fp.get16(0)&0x3f)!=0x3f || (!clipping && cpu.r[wr::EBX]>17) ||
       std::uint64_t(cpu.r[wr::ESP])+GridFrame::Size>0x100000000ull)return false;
    auto lock=memory.execution_scope();GridFrame frame;frame.base=cpu.r[wr::ESP];
    try{
        memory.check(frame.base,frame.Size,wr::Memory::Read|wr::Memory::Write);
        for(std::uint64_t at=frame.base;at<std::uint64_t(frame.base)+frame.Size;at=(at/4096+1)*4096)
            if(memory.permissions(wr::U32(at))&wr::Memory::Execute)return false;
        memory.copy_out(frame.base,frame.bytes);
        if(!clipping && frame.load<32>(frame.base+0x78)>17)return false;
        const auto resource=frame.load<32>(frame.base+(clipping?0x64:0x34));
        if(resource>0xffffff97u || (clipping && cpu.r[wr::EDI]!=resource))return false;
        frame.resource=resource+0x60;
        const auto aliases_frame=[&](wr::U32 address,unsigned size){return std::uint64_t(address)<std::uint64_t(frame.base)+frame.Size && std::uint64_t(address)+size>frame.base;};
        if(aliases_frame(frame.resource,frame.dimensions.size()))return false;
        memory.copy_out(frame.resource,frame.dimensions);
        if(clipping){
            if(frame.load<16>(frame.resource+4)>16 || frame.load<16>(frame.resource+6)>16)return false;
            frame.sprite=frame.load<32>(frame.base+0x94);
            if(!frame.sprite || std::uint64_t(frame.sprite)+frame.sprite_fields.size()>0x100000000ull || aliases_frame(frame.sprite,frame.sprite_fields.size()))return false;
            memory.copy_out(frame.sprite,frame.sprite_fields);
        }
        bool memo=memo_enabled() && clipping && frame.sprite>frame.base && frame.sprite-frame.base<0x4000 && (cpu.fp.get16(0)&0xf3f)==0x3f;
#if WA2_AOT_DIAGNOSTICS
        memo=memo && native_grid_registers_enabled && native_grid_float_enabled;
#endif
        GridMemo::Pending pending{};
        if(memo){
            std::array<std::uint8_t,GridFrame::Size+8+256+12> input;
            std::memcpy(input.data(),frame.bytes.data(),frame.bytes.size());std::memcpy(input.data()+frame.bytes.size(),frame.dimensions.data(),8);
            std::memcpy(input.data()+frame.bytes.size()+8,frame.sprite_fields.data(),256);
            std::memcpy(input.data()+frame.bytes.size()+264,&frame.base,4);std::memcpy(input.data()+frame.bytes.size()+268,&frame.resource,4);std::memcpy(input.data()+frame.bytes.size()+272,&frame.sprite,4);
            if(grid_memo.lookup(cpu,input,frame.bytes,pending)){
                memory.copy_in(frame.base,frame.bytes);++memo_counts.grid_hits;
                if(sample){++sample->vertex_grid_completed;if(clipping)++sample->vertex_grid_clipping_completed;}return true;
            }
            ++memo_counts.grid_misses;
        }
        auto result=cpu;bool native_float=(cpu.fp.get16(0)&0xf3f)==0x3f;
#if WA2_AOT_DIAGNOSTICS
        native_float=native_float && native_grid_float_enabled;
#endif
        if(native_float){
            if(sample)++sample->vertex_grid_native_attempts;
            const auto original_frame=frame.bytes;
            try{
                GeometryEnvironment environment;
#if WA2_AOT_DIAGNOSTICS
                if(!native_grid_registers_enabled)calculate_grid<GridNativeFloating<true,false>>(result,frame);else
#endif
                if(memo && pending.capture){++memo_counts.grid_captures;TracingFrame<GridFrame> tracked(frame);
                    calculate_grid<GridNativeFloating<true,true,false,true,TracingFrame<GridFrame>>>(result,tracked);
                    frame.bytes=tracked.bytes;grid_memo.commit(pending,result,frame.bytes,tracked.trace.inputs(GridFrame::Size+8+256+12),tracked.trace.outputs());
                }else calculate_grid<GridNativeFloating<true,true,false,true>>(result,frame);
                if(sample)++sample->vertex_grid_native_completed;
            }catch(const GridDeclined&){frame.bytes=original_frame;result=cpu;native_float=false;}
        }
        if(!native_float)calculate_grid<GridFloating>(result,frame);
        memory.copy_in(frame.base,frame.bytes);cpu=result;
        if(sample)++sample->vertex_grid_completed;
        if(sample && clipping)++sample->vertex_grid_clipping_completed;
        return true;
    }catch(const GridDeclined&){return false;}catch(const wr::GuestFault&){return false;}
}
}
