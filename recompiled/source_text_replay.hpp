#pragma once
#include "text_chunk_cache.hpp"
#include "text_page_packet.hpp"
#include "glyph_budget.hpp"
#include "host_calls.hpp"
#include "text_page_replay_control.hpp"
#include "text_layout_replay_control.hpp"
#include "fp_execution.hpp"
#include <bit>
#include <cstring>

namespace wa2::aot {
// Source ABI data only. Both hosts run this same bounded memo and packet path.
struct SourceTextProfile {
    wr::U32 entry,layout_begin,outline_end,fill_end,page_begin,glyph_call,glyph_return,glyph_entry,remainder,feature,font_metrics;
    unsigned frame_bytes,frame_capture,argument_bytes;
    int sprite_slot,string_slot,cursor_slot,output_slot,ruby_slot;
    std::span<const std::pair<wr::U32,unsigned>> globals;
    SpriteSource drawing;
};
struct SourceTextCounts {std::uint64_t layout_hits{},layout_captures{},page_spans{},page_glyphs{},page_captures{},cancelled{};};
inline thread_local SourceTextCounts source_text_counts;
namespace source_text {
using Word=wr::U32;using Scope=wr::Memory::ExecutionScope;using View=std::span<const std::uint8_t>;
inline bool overlap(View a,View b){if(a.empty() || b.empty())return false;const auto x=reinterpret_cast<std::uintptr_t>(a.data()),y=reinterpret_cast<std::uintptr_t>(b.data());return x<=y?y-x<a.size():x-y<b.size();}
inline bool span(Scope& m,Word at,std::span<std::uint8_t> bytes,bool write=false){
    if(std::uint64_t(at)+bytes.size()>0x100000000ull)return false;
    while(!bytes.empty()){
        const auto count=unsigned(std::min<std::size_t>(bytes.size(),4096-(at&4095)));
        const auto* page=fp::detail::operand_page(m,at,count,wr::Memory::Read|(write?wr::Memory::Write:0));
        if(!page || (write && (page->permissions&wr::Memory::Execute)))return false;
        if(write)m.write_bytes(at,bytes.first(count));else std::memcpy(bytes.data(),page->data+(at&4095),count);
        bytes=bytes.subspan(count);at+=count;
    }return true;
}
inline bool independent(Scope& m,Word at,unsigned bytes,Word stack,unsigned stack_bytes){
    if(std::uint64_t(at)+bytes>0x100000000ull || std::uint64_t(stack)+stack_bytes>0x100000000ull)return false;
    for(unsigned done=0;done<bytes;){
        const auto addr=at+done,part=std::min(bytes-done,4096-(addr&4095));const auto v=m.readable(addr,part);if(v.size()!=part)return false;
        for(unsigned offset=0;offset<stack_bytes;){const auto sp=stack+offset,n=std::min(stack_bytes-offset,4096-(sp&4095));const auto s=m.readable(sp,n);if(s.size()!=n || overlap(v,s))return false;offset+=n;}done+=part;
    }return true;
}
struct CpuState {
    std::array<Word,8> regs;Word flags,defined;std::array<std::uint8_t,512> fp;
    bool operator==(const CpuState&)const=default;
    static CpuState read(const wr::Cpu& c){CpuState s{c.r,c.flags,c.defined_flags,{}};std::memcpy(s.fp.data(),c.fp.bytes.data(),s.fp.size());return s;}
    void apply(wr::Cpu& c)const{c.r=regs;c.flags=flags;c.defined_flags=defined;std::memcpy(c.fp.bytes.data(),fp.data(),fp.size());}
};
inline bool frame(Scope& m,const wr::Cpu& c,const SourceTextProfile& p,bool page){
    const auto bp=c.r[wr::EBP];if((c.flags&wr::DF) || (bp&3) || bp<p.frame_capture || bp>0xffffffffu-8-p.argument_bytes ||
        c.r[wr::ESP]!=bp-p.frame_bytes-(page?0x124u:0u) || c.fp.bytes[4] || (c.fp.get16(0)&0x3f)!=0x3f || (c.fp.get16(2)&0x8080))return false;
    for(auto start=bp-p.frame_capture;start<bp;){const auto n=std::min(bp-start,4096-(start&4095));const auto* a=fp::detail::operand_page(m,start,n,3);if(!a || (a->permissions&4))return false;start+=n;}
    for(auto start=bp-p.frame_capture;start<bp;){const auto n=std::min(bp-start,4096-(start&4095));
        if(!independent(m,start,n,start+n,bp-start-n))return false;start+=n;}
    const auto* f=fp::detail::operand_page(m,p.feature,4,wr::Memory::Read);
    return f && m.load<32>(p.feature)!=0;
}
struct LayoutKey {
    std::array<Word,22> values;std::array<std::uint8_t,512> fp;std::array<std::uint8_t,64> stack;
    std::uint64_t epoch;
    bool operator==(const LayoutKey&)const=default;
};
struct LayoutEntry {
    LayoutKey key;CpuState result;std::array<std::uint8_t,64> stack;std::array<Word,5> locals;Word end{};
};
struct LayoutSet {std::array<LayoutEntry,4> entries;unsigned next{};};
inline thread_local std::unique_ptr<std::array<LayoutSet,256>> layouts;
// The transition key includes every byte of its writable frame and every
// source argument. A replay can therefore restore the complete footprint,
// including unchanged stores, without overwriting an unrelated live byte.
struct PageKey {
    CpuState cpu;std::array<std::uint8_t,688> frame;std::array<std::uint8_t,104> args;
    std::array<std::uint8_t,32> text;std::array<std::uint8_t,1024> globals;
    Word base{},cursor{},sprite{},remainder{};std::uint64_t epoch{};
    bool operator==(const PageKey&)const=default;
};
struct ResourceKey {
    std::array<std::uint8_t,256> sprite,ranges;std::array<std::uint8_t,128> image;
    std::array<std::uint8_t,1024> handles;std::array<std::uint8_t,52> metrics;std::array<Word,12> globals;
    Word number{};
    bool operator==(const ResourceKey&)const=default;
};
struct Step {PageKey key;CpuState before,after;std::array<std::uint8_t,688> output;Word remainder{};TextPagePacket draw;bool finished{},returned{};};
struct Chunk {std::array<Step,8> steps;ResourceKey resources;unsigned count{},geometry_slot{128};std::uint64_t serial{};};
inline thread_local text_chunks::ChunkCache<Chunk> chunks;
inline bool resource(Scope& m,const SourceTextProfile& p,const PageKey& k,Word number,ResourceKey& r){
    if(number>=1024)return false;r.number=number;const auto bp=k.cpu.regs[wr::EBP],low=bp-p.frame_capture;
    auto read=[&](Word at,auto& out){return independent(m,at,sizeof(out),low,p.frame_capture) && independent(m,at,sizeof(out),p.remainder,4) && span(m,at,{reinterpret_cast<std::uint8_t*>(&out),sizeof(out)});};
    if(!read(k.sprite,r.sprite) || !read(p.drawing.font_ranges,r.ranges) || !read(p.drawing.images+number*128,r.image))return false;
    auto word=[](const std::uint8_t* b){Word v;std::memcpy(&v,b,4);return v;};
    auto half=[](const std::uint8_t* b){return unsigned(b[0])|(unsigned(b[1])<<8);};
    if(!(half(r.sprite.data()+0x48)&0x8000) || word(r.sprite.data()+0x74))return false;
    const auto style=wr::sign_extend(half(r.sprite.data()+0x50),16);const auto palette=std::bit_cast<std::int8_t>(r.sprite[0x52]);
    if(!read(p.font_metrics-1+style*0x34,r.metrics))return false;
    const auto g=p.drawing.globals;
    const std::array<Word,12> addresses{g,g+4,g+0x428,g+0x43c,g+0x438,g+0x42c,g+0x2c0,g+0x2a0,p.drawing.half_pixel,Word(p.drawing.palette+int(palette)*4),p.feature,p.drawing.trig_dispatch};
    for(unsigned n=0;n<addresses.size();++n)if(!read(addresses[n],r.globals[n]))return false;
    const auto nx=half(r.image.data()+0x64),ny=half(r.image.data()+0x66);const auto address=word(r.image.data()+0x70);r.handles.fill(0);
    return nx && nx<=16 && ny && ny<=16 && independent(m,address,nx*ny*4,low,p.frame_capture) && independent(m,address,nx*ny*4,p.remainder,4) && span(m,address,std::span(r.handles).first(nx*ny*4));
}
}

class SourceTextReplay {
    using Word=wr::U32;using Scope=wr::Memory::ExecutionScope;
    wr::Memory& memory_;const SourceTextProfile& p_;bool enabled_;
    source_text::LayoutEntry* layout_{};source_text::Chunk* recording_{};bool pending_{};SpriteService* service_{};
    bool key(wr::Cpu& cpu,Scope& m,source_text::PageKey& k){
        try{
        using namespace source_text;if(!frame(m,cpu,p_,true))return false;const auto bp=cpu.r[wr::EBP];
        if(p_.frame_capture!=k.frame.size() || p_.argument_bytes!=k.args.size() || m.load<32>(bp+p_.output_slot) || m.load<32>(bp+p_.ruby_slot))return false;
        k.cpu=CpuState::read(cpu);k.epoch=memory_.code_epoch();k.base=m.load<32>(bp+p_.string_slot);k.cursor=m.load<32>(bp+p_.cursor_slot);k.sprite=m.load<32>(bp+p_.sprite_slot);k.remainder=m.load<32>(p_.remainder);
        if(std::uint64_t(k.base)+k.cursor+2+k.text.size()>0x100000000ull || !independent(m,k.base+k.cursor+2,k.text.size(),bp-p_.frame_capture,p_.frame_capture) ||
            !independent(m,p_.remainder,4,bp-p_.frame_capture,p_.frame_capture))return false;
        const auto* page=fp::detail::operand_page(m,p_.remainder,4,3);if(!page || (page->permissions&4))return false;
        if(!independent(m,bp+8,k.args.size(),bp-p_.frame_capture,p_.frame_capture) || !independent(m,bp+8,k.args.size(),p_.remainder,4) ||
            !independent(m,k.base+k.cursor+2,k.text.size(),p_.remainder,4) || !span(m,bp-p_.frame_capture,k.frame) || !span(m,bp+8,k.args) || !span(m,k.base+k.cursor+2,k.text))return false;
        // Ordinary two-byte glyphs only. Markup, ASCII, ruby and output
        // buffers retain the checked source path and its exact exits.
        const auto lead=k.text[0],trail=k.text[1];if(!((lead>=0x81 && lead<=0x9f)||(lead>=0xe0 && lead<=0xfc)) || trail<0x40 || trail==0x7f || trail>0xfc)return false;
        k.globals.fill(0);unsigned offset{};
        for(auto [at,size]:p_.globals){if(size>k.globals.size()-offset || !independent(m,at,size,bp-p_.frame_capture,p_.frame_capture) || !span(m,at,std::span(k.globals).subspan(offset,size)))return false;offset+=size;}
        return true;}catch(const wr::GuestFault&){return false;}
    }
    void complete(wr::Cpu& cpu,Scope& m){
        if(!recording_ || !pending_)return;auto& step=recording_->steps[recording_->count];pending_=false;text_page_observer={};
        if(!step.finished || !step.draw.captured || cpu.eip!=p_.page_begin){cancel();return;}
        step.after=source_text::CpuState::read(cpu);step.returned=true;++recording_->count;
        if(recording_->count==recording_->steps.size())cancel();
    }
public:
    SourceTextReplay(wr::Memory& memory,const SourceTextProfile& profile,bool enabled):memory_(memory),p_(profile),enabled_(enabled){
        if(enabled && text_page_replay_enabled.load(std::memory_order_relaxed)){auto* s=SpriteService::find(memory);if(s && s->read_only_glyphs())service_=s;}
    }
    ~SourceTextReplay(){cancel();}
    void cancel(){if(recording_ && pending_)text_page_observer={};if(recording_ && recording_->count>=2 && !recording_->serial){recording_->serial=text_chunks::serial(recording_->geometry_slot);++source_text_counts.page_captures;}recording_=nullptr;pending_=false;}
    bool page_recording()const{return recording_!=nullptr;}
    void abandon(){if(recording_)++source_text_counts.cancelled;cancel();}
    Word layout_begin(wr::Cpu& cpu,Scope& m){
        try{
        using namespace source_text;layout_=nullptr;if(!enabled_ || !text_layout_replay_enabled.load(std::memory_order_relaxed) || !frame(m,cpu,p_,false))return 0;
        const auto bp=cpu.r[wr::EBP],sp=cpu.r[wr::ESP],sprite=m.load<32>(bp+p_.sprite_slot);
        if(std::uint64_t(sprite)+0x52>0x100000000ull || !independent(m,sprite+0x50,2,bp-p_.frame_capture,p_.frame_capture))return 0;
        const auto style=wr::sign_extend(m.load<16>(sprite+0x50),16),metric=p_.font_metrics+style*0x34;
        if(!independent(m,metric,3,bp-p_.frame_capture,p_.frame_capture))return 0;
        LayoutKey k;k.epoch=memory_.code_epoch();
        k.values={sp,bp,cpu.r[wr::EBX],m.load<32>(bp-0xd0),m.load<32>(bp-0xd8),m.load<32>(bp-0xe4),m.load<32>(bp-0xcc),
            Word(m.load<8>(metric)|(m.load<8>(metric+1)<<8)|(m.load<8>(metric+2)<<16)),m.load<32>(bp+0x50),
            m.load<8>(bp+0x54)|(m.load<8>(bp+0x58)<<8)|(m.load<8>(bp+0x5c)<<16),
            m.load<8>(bp-0x104)|(m.load<8>(bp-0x10c)<<8)|(m.load<8>(bp-0x108)<<16),cpu.flags,cpu.defined_flags,p_.entry,
            cpu.r[wr::EAX],cpu.r[wr::ECX],cpu.r[wr::EDX],cpu.r[wr::ESI],cpu.r[wr::EDI],m.load<32>(bp+p_.string_slot),0,0};
        std::memcpy(k.fp.data(),cpu.fp.bytes.data(),k.fp.size());if(!span(m,sp-64,k.stack))return 0;
        if(!layouts){const auto saved=errno;layouts.reset(new(std::nothrow) std::array<LayoutSet,256>{});errno=saved;if(!layouts)return 0;}
        Word hash=bp^std::rotl(k.values[3]*0x9e3779b9u,9)^std::rotl(k.values[4]*0x85ebca77u,13)^k.values[7]^k.values[8];auto& set=(*layouts)[(hash^(hash>>16))&255];
        for(auto& e:set.entries)if(e.end && e.key==k){span(m,sp-64,e.stack,true);const std::array<int,5> slots{-0xdc,-0xfc,-0xf0,-0x110,-0xf4};for(unsigned n=0;n<slots.size()-(k.values[8]?1:0);++n)m.store<32>(bp+slots[n],e.locals[n]);e.result.apply(cpu);++source_text_counts.layout_hits;return e.end;}
        layout_=&set.entries[set.next++%set.entries.size()];layout_->key=k;layout_->end=0;return 0;
        }catch(const wr::GuestFault&){layout_=nullptr;return 0;}
    }
    void layout_finish(wr::Cpu& cpu,Scope& m,Word pc){
        if(!layout_)return;auto* e=layout_;layout_=nullptr;const auto bp=e->key.values[1],sp=e->key.values[0];const bool outline=e->key.values[8]!=0;
        if(pc!=(outline?p_.outline_end:p_.fill_end) || cpu.r[wr::ESP]!=sp-(outline?24u:28u) || cpu.fp.bytes[4])return;
        e->result=source_text::CpuState::read(cpu);if(!source_text::span(m,sp-64,e->stack))return;
        const std::array<int,5> slots{-0xdc,-0xfc,-0xf0,-0x110,-0xf4};for(unsigned n=0;n<slots.size();++n)e->locals[n]=m.load<32>(bp+slots[n]);e->end=pc;++source_text_counts.layout_captures;
    }
    Word page_begin(wr::Cpu& cpu,Scope& m){
        using namespace source_text;if(!service_)return 0;complete(cpu,m);PageKey k;if(!key(cpu,m,k)){abandon();return 0;}
        if(recording_){const auto& first=recording_->steps[0].key;if(k.base!=first.base || k.sprite!=first.sprite || k.cursor!=first.cursor+2*recording_->count){abandon();return 0;}}
        else{
            Word h=k.base^k.cursor*0x9e3779b9u^std::rotl(k.cpu.regs[wr::EBP],11);auto selected=chunks.select(h,h^(h>>16),true,[&](Chunk& c){ResourceKey r;return c.count>=2 && c.steps[0].key==k && resource(m,p_,k,c.resources.number,r) && r==c.resources;});
            if(selected.hit){unsigned count=selected.chunk->count;auto* remaining=GlyphCallScope::remaining(memory_);if(!remaining && current_native_calls)remaining=&current_native_calls->remaining;if(remaining)count=std::min(count,*remaining);if(count<2)return 0;
                for(unsigned n=0;n<count;++n){auto& step=selected.chunk->steps[n];if(!step.finished || !step.returned)return 0;span(m,k.cpu.regs[wr::EBP]-p_.frame_capture,step.output,true);m.store<32>(p_.remainder,step.remainder);step.before.apply(cpu);
                    const auto sp=cpu.r[wr::ESP];cpu.r[wr::ESP]=sp-4;m.store<32>(sp-4,p_.glyph_return);cpu.eip=p_.glyph_entry;auto command=step.draw.command;command.tiles=std::span(step.draw.tiles).first(step.draw.size);
                    if(!service_->submit_prepared(command,selected.chunk->serial,n)){cpu.r[wr::ESP]=sp;cpu.eip=p_.glyph_call;return p_.glyph_call;}
                    if(remaining)--*remaining;count_page_glyph(step.draw.size,(cpu.fp.get16(0)&0x300)!=0);step.after.apply(cpu);cpu.eip=p_.page_begin;++source_text_counts.page_glyphs;
                }++source_text_counts.page_spans;return p_.page_begin;
            }
            if(!selected.chunk)return 0;recording_=selected.chunk;
        }
        auto& step=recording_->steps[recording_->count];step.key=k;step.draw.captured=false;step.finished=step.returned=false;pending_=true;return 0;
    }
    void page_finish(wr::Cpu& cpu,Scope& m){
        try{
        using namespace source_text;if(!recording_ || !pending_)return;auto& step=recording_->steps[recording_->count];const auto bp=step.key.cpu.regs[wr::EBP];
        if(cpu.r[wr::ESP]!=step.key.cpu.regs[wr::ESP] || cpu.r[wr::EBP]!=bp || cpu.fp.bytes[4] || !span(m,bp-p_.frame_capture,step.output)){abandon();return;}
        step.before=CpuState::read(cpu);step.remainder=m.load<32>(p_.remainder);const auto sprite=step.key.sprite;
        const auto number=std::uint16_t(m.load<16>(sprite+0x4a)+m.load<32>(cpu.r[wr::ESP]+256+32));
        ResourceKey r;if(!resource(m,p_,step.key,number,r) || (recording_->count && !(r==recording_->resources))){abandon();return;}
        if(!recording_->count)recording_->resources=r;step.finished=true;text_page_observer={&memory_,cpu.r[wr::ESP]-4,p_.glyph_return,&step.draw};
        }catch(const wr::GuestFault&){abandon();}
    }
};
}
