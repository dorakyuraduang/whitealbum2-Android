#pragma once
#include "text_layout_replay.hpp"
#include "text_run_replay_control.hpp"
#include "../engine/byte_equality.hpp"
#include <cerrno>

namespace wa2::aot {
namespace text_run {
using Word=wr::U32;
using Scope=wr::Memory::ExecutionScope;
using View=std::span<const std::uint8_t>;
constexpr Word fill_call=0x4473d3,outline_call=0x447310,packet_size=0x124;

// At 4473d8 ESP points to the previous glyph's by-value packet. The source
// discards the packet immediately. Through the next CALL it only reads from
// these two local/argument windows, the next code unit, the font metrics,
// the SSE selector and 445350's fractional text-speed accumulator.
struct Key {
    std::array<std::uint8_t,0x60> locals; // original frame +0x18 .. +0x77
    std::array<std::uint8_t,0x58> arguments; // +0x150 .. +0x1a7
    Word sp,bp,code,metrics,remainder,x87,mxcsr;
    bool matches(const Key& o)const {
        return sp==o.sp && bp==o.bp && code==o.code && metrics==o.metrics &&
            remainder==o.remainder && x87==o.x87 && mxcsr==o.mxcsr &&
            wa2::memory::equal_bytes<0x60>(locals.data(),o.locals.data()) &&
            wa2::memory::equal_bytes<0x58>(arguments.data(),o.arguments.data());
    }
    static Word word(const std::uint8_t* p){Word v;std::memcpy(&v,p,4);return v;}
    Word local(unsigned off)const{return word(locals.data()+off-0x18);}
    Word arg(unsigned off)const{return word(arguments.data()+off-0x150);}
};
struct End {
    std::array<Word,8> regs;
    std::array<std::uint8_t,160> fp;
    std::array<std::uint8_t,36> args;
    Word coordinate[3],scratch,x,opacity,time,remainder,flags,defined,pc{};
};
struct Entry {Key key;End end;};
struct Set {std::array<Entry,4> entries;unsigned next{};};
inline thread_local std::unique_ptr<std::array<Set,256>> table;

inline bool overlap(View a,View b){
    if(a.empty() || b.empty())return false;
    const auto x=reinterpret_cast<std::uintptr_t>(a.data()),y=reinterpret_cast<std::uintptr_t>(b.data());
    return x<=y ? y-x<a.size() : x-y<b.size();
}
inline View writable(Scope& scope,Word address,unsigned bytes){
    constexpr unsigned rw=wr::Memory::Read|wr::Memory::Write;
    const auto* page=fp::detail::operand_page(scope,address,bytes,rw);
    if(!page || (page->permissions&wr::Memory::Execute))return {};
    return {page->data+(address&4095),bytes};
}
inline bool inputs(wr::Cpu& cpu,Scope& scope,Key& key,View& sprite){
    if constexpr(std::endian::native!=std::endian::little)return false;
    if((cpu.flags&wr::DF) || cpu.fp.bytes[4] || (cpu.fp.get16(0)&0x3f)!=0x3f ||
       (cpu.fp.get16(2)&0x8080) || (cpu.fp.get32(24)&0x1f80)!=0x1f80)return false;
    const Word start=cpu.r[wr::ESP];
    constexpr unsigned bytes=packet_size+0x1a8;
    if((start&3) || start>0xffffffffu-bytes)return false;
    const Word frame=start+packet_size;
    const unsigned first=std::min(bytes,4096u-(start&4095));
    const auto a=writable(scope,start,first);
    const auto b=first==bytes?View{}:writable(scope,start+first,bytes-first);
    if(a.empty() || (first<bytes && b.empty()) || overlap(a,b))return false;
    const auto remainder=writable(scope,0xb34544,4);
    if(remainder.empty() || overlap(a,remainder) || overlap(b,remainder))return false;
    text_layout::copy_out(scope,frame+0x18,key.locals);
    text_layout::copy_out(scope,frame+0x150,key.arguments);
    if(key.local(0x5c) || key.local(0x70) || key.local(0x74))return false;
    key.bp=cpu.r[wr::EBP];key.sp=start;
    const auto next=std::uint64_t(key.local(0x20))+key.bp+2;
    if(next>0xffffffffu-1)return false;
    const auto raw=scope.readable(Word(next),2);
    sprite=scope.readable(key.arg(0x150),256);
    if(raw.size()!=2 || sprite.size()!=256)return false;
    const unsigned lead=raw[0],trail=raw[1];
    if(!((lead>=0x81 && lead<=0x9f) || (lead>=0xe0 && lead<=0xfc)) ||
        trail<0x40 || trail==0x7f || trail>0xfc)return false;
    const Word style=wr::sign_extend(unsigned(sprite[0x50])|(unsigned(sprite[0x51])<<8),16);
    const auto metrics=scope.readable(0xa823b1u+style*0x34u,3);
    const auto feature=scope.readable(0xbc8310,4);
    if(metrics.size()!=3 || feature.size()!=4 || !Key::word(feature.data()))return false;
    // A by-value sprite can belong to the caller's stack. Reject only ranges
    // overlapping this call frame (even when two guest mappings share storage).
    for(const auto source:{raw,sprite,metrics,feature})
        if(overlap(a,source) || overlap(b,source) || overlap(remainder,source))return false;
    key.code=lead|(trail<<8);
    key.metrics=metrics[0]|(Word(metrics[1])<<8)|(Word(metrics[2])<<16);
    key.remainder=Key::word(remainder.data());
    key.x87=cpu.fp.get32(0);key.mxcsr=cpu.fp.get32(24);
    return true;
}
inline void apply(const Entry& e,wr::Cpu& cpu,Scope& scope,View sprite){
    const auto& k=e.key;const auto& end=e.end;
    const Word frame=k.sp+packet_size;
    // Each original full packet overwrites all 292 bytes. Read the CURRENT
    // sprite, so new coordinates, animation and texture references pass through.
    scope.write_bytes(k.sp,sprite);
    scope.write_bytes(k.sp+256,end.args);
    scope.store<32>(frame+0x18,end.scratch);
    scope.store<32>(frame+0x24,end.x);
    scope.store<32>(frame+0x34,end.opacity);
    const bool outline=end.pc==outline_call;
    scope.write_bytes(frame+0x3c,{reinterpret_cast<const std::uint8_t*>(end.coordinate),outline?8u:12u});
    if(end.time!=k.local(0x58))scope.store<32>(frame+0x58,end.time);
    if(k.local(0x30)!=0xfffffffeu)scope.store<32>(0xb34544,end.remainder);
    cpu.r=end.regs;
    cpu.flags=(cpu.flags&~wr::STATUS_FLAGS)|end.flags;
    cpu.defined_flags=(cpu.defined_flags&~wr::STATUS_FLAGS)|end.defined;
    // From the previous CALL to the next, only the identical checked 44722c
    // layout block uses FP. Preserve all unused ST payloads and the XMM bank.
    std::memcpy(cpu.fp.bytes.data()+2,end.fp.data()+2,3);
    std::memcpy(cpu.fp.bytes.data()+6,end.fp.data()+6,26);
    const unsigned first=outline?6:3;
    for(unsigned i=0;i<first;++i)cpu.fp.clear_x87_padding(i);
    const unsigned offset=32+16*first;
    std::memcpy(cpu.fp.bytes.data()+offset,end.fp.data()+offset,160-offset);
}
}

class TextRunReplay {
    text_run::Entry* pending_{};
public:
    const bool enabled{text_run_replay_enabled.load(std::memory_order_relaxed)};
    bool recording()const{return pending_!=nullptr;}
    void cancel(){pending_=nullptr;}
    wr::U32 begin(wr::Cpu& cpu,wr::Memory::ExecutionScope& scope){
        using namespace text_run;
        cancel();
        Key k;View sprite;
        if(!inputs(cpu,scope,k,sprite))return 0;
        if(!table){
            const int saved=errno;
            table.reset(new(std::nothrow) std::array<Set,256>{});
            errno=saved;
            if(!table)return 0;
        }
        Word hash=k.bp*0x9e3779b9u ^ std::rotl(k.local(0x24)*0x85ebca77u,13) ^
            std::rotl(k.local(0x1c)*0xc2b2ae3du,7) ^ k.arg(0x198)*0x27d4eb2fu ^ k.sp;
        auto& set=(*table)[(hash^(hash>>16))&255u];
        for(const auto& e:set.entries)if(e.end.pc && e.key.matches(k)){
            apply(e,cpu,scope,sprite);++text_run_replay_hits;return e.end.pc;
        }
        pending_=&set.entries[set.next++%set.entries.size()];
        pending_->key=k;pending_->end.pc=0;
        return 0;
    }
    void finish(wr::Cpu& cpu,wr::Memory::ExecutionScope& scope,wr::U32 pc){
        using namespace text_run;
        if(!pending_)return;
        auto& e=*pending_;cancel();
        const auto& k=e.key;const Word frame=k.sp+packet_size;
        if(cpu.r[wr::ESP]!=k.sp || cpu.r[wr::EBP]!=k.bp+2 || cpu.fp.bytes[4] ||
           scope.load<32>(frame+0x34)!=k.arg(0x194) ||
           pc!=(k.arg(0x198)?outline_call:fill_call))return;
        auto& end=e.end;
        end.regs=cpu.r;end.flags=cpu.flags&wr::STATUS_FLAGS;end.defined=cpu.defined_flags&wr::STATUS_FLAGS;
        std::memcpy(end.fp.data(),cpu.fp.bytes.data(),end.fp.size());
        text_layout::copy_out(scope,k.sp+256,end.args);
        end.scratch=scope.load<32>(frame+0x18);end.x=scope.load<32>(frame+0x24);
        end.opacity=scope.load<32>(frame+0x34);end.time=scope.load<32>(frame+0x58);
        end.remainder=scope.load<32>(0xb34544);
        for(unsigned n=0;n<3;++n)end.coordinate[n]=scope.load<32>(frame+0x3c+n*4);
        end.pc=pc;++text_run_replay_captures;
    }
};
}
