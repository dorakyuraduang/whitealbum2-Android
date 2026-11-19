#pragma once
#include "text_run_replay.hpp"
#include "text_page_packet.hpp"
#include "text_page_replay_control.hpp"
#include "glyph_budget.hpp"
#include "host_calls.hpp"

namespace wa2::aot::text_page {
using namespace text_run;
constexpr unsigned Width=8;
struct Step {Entry transition;TextPagePacket draw;};

// These are the resource reads made by the admitted unrotated surface glyph
// builder. Keep exact bytes (including empty texture handles). A changed
// atlas mapping, style, filter, source sprite or target takes the source path.
struct Resources {
    std::array<std::uint8_t,256> sprite,ranges;
    std::array<std::uint8_t,128> image;
    std::array<std::uint8_t,1024> handles;
    std::array<std::uint8_t,3> metrics;
    std::array<Word,11> globals;
    Word image_number{};
};
struct Chunk {
    std::array<Step,Width> steps;
    Resources inputs;
    unsigned count{};
    std::uint64_t serial{};
};
struct Slot {std::array<std::unique_ptr<Chunk>,2> ways;unsigned next{};};
inline thread_local std::array<Slot,64> chunks;
inline std::atomic<std::uint64_t> next_serial{1};
inline std::uint64_t serial(){
    auto n=next_serial.load(std::memory_order_relaxed);
    while(n && !next_serial.compare_exchange_weak(n,n+1,std::memory_order_relaxed)){}
    return n;
}

struct Footprint {
    View lower,upper,remainder;
    bool prepare(Scope& scope,Word sp){
        if(sp<4)return false;
        const Word start=sp-4,bytes=4+packet_size+0x1a8;
        const unsigned first=std::min(bytes,4096u-(start&4095));
        lower=writable(scope,start,first);
        upper=first<bytes?writable(scope,start+first,bytes-first):View{};
        remainder=writable(scope,0xb34544,4);
        return !lower.empty() && (bytes==first || !upper.empty()) && !remainder.empty() &&
            !overlap(lower,upper) && !overlap(lower,remainder) && !overlap(upper,remainder);
    }
    bool independent(View source)const{
        return !source.empty() && !overlap(lower,source) && !overlap(upper,source) && !overlap(remainder,source);
    }
    bool bytes(Scope& scope,Word at,std::span<std::uint8_t> data,bool capture)const{
        if(std::uint64_t(at)+data.size()>0x100000000ULL)return false;
        while(!data.empty()){
            auto view=scope.readable(at,data.size());
            if(!independent(view))return false;
            if(capture)std::memcpy(data.data(),view.data(),view.size());
            else if(std::memcmp(data.data(),view.data(),view.size()))return false;
            data=data.subspan(view.size());at+=Word(view.size());
        }
        return true;
    }
};
inline bool resources(Scope& scope,const Footprint& guard,const Key& k,Resources& r,bool capture){
    auto bytes=[&](Word address,auto& dest){
        return guard.bytes(scope,address,{reinterpret_cast<std::uint8_t*>(&dest),sizeof(dest)},capture);
    };
    if(!bytes(k.arg(0x150),r.sprite) || !bytes(0x4a14b8,r.ranges))return false;
    auto u16=[](const std::uint8_t* p){return Word(p[0])|(Word(p[1])<<8);};
    if((u16(&r.sprite[0x48])&0x8000)==0 || Key::word(&r.sprite[0x74]) || r.image_number>=1024)return false;
    const Word style=wr::sign_extend(u16(&r.sprite[0x50]),16);
    const auto palette=std::bit_cast<std::int8_t>(r.sprite[0x52]);
    const std::array<Word,11> addresses{0xb34000,0xb34004,0xb34428,0xb3443c,0xb34438,0xb3442c,
        0xb342c0,0xb342a0,0x4c2280,Word(0x4c2290+int(palette)*4),0xbc8310};
    for(unsigned n=0;n<addresses.size();++n)if(!bytes(addresses[n],r.globals[n]))return false;
    if(!bytes(0xa823b1+style*0x34,r.metrics) || !bytes(0xa93fb8+r.image_number*128,r.image))return false;
    const unsigned nx=u16(&r.image[0x64]),ny=u16(&r.image[0x66]);
    return nx && nx<=16 && ny && ny<=16 &&
        guard.bytes(scope,Key::word(&r.image[0x70]),std::span(r.handles).first(nx*ny*4),capture);
}
}

namespace wa2::aot {
class TextPageReplay {
    using Word=wr::U32;
    using Scope=wr::Memory::ExecutionScope;
    text_page::Chunk* recording_{};
    bool pending_{};
    SpriteService* service_{};
    wr::Memory& memory_;

    void finish_draw(){
        if(!recording_ || !pending_)return;
        auto& step=recording_->steps[recording_->count];
        if(step.draw.captured && step.transition.end.pc)++recording_->count;
        else recording_=nullptr;
        pending_=false;text_page_observer={};
        if(recording_ && recording_->count==text_page::Width)cancel();
    }
    static unsigned matching_text(Scope& scope,const text_page::Footprint& guard,
                                 const text_page::Chunk& chunk,const text_run::Key& k){
        Word address=k.local(0x20)+k.bp+2;
        const auto chars=scope.readable(address,2*chunk.count);
        if(!guard.independent(chars))return 0;
        unsigned n=0;
        for(;n<chunk.count && n*2+2<=chars.size();++n){
            if((Word(chars[n*2])|(Word(chars[n*2+1])<<8))!=chunk.steps[n].transition.key.code)break;
        }
        return n;
    }
    Word replay(wr::Cpu& cpu,Scope& scope,text_page::Chunk& chunk,text_run::View sprite,unsigned count){
        using namespace text_run;
        auto* remaining=GlyphCallScope::remaining(memory_);
        if(!remaining && current_native_calls)remaining=&current_native_calls->remaining;
        if(remaining)count=std::min(count,*remaining);
        if(count<2)return 0;
        unsigned admitted=0;
        for(unsigned n=0;n<count;++n){
            const auto& step=chunk.steps[n];const auto& source=step.transition;
            const auto before=source.end.pc;
            const Word returning=before==outline_call?0x447315:0x4473d8;
            // Materialize each architectural boundary before a real host
            // call. A diagnostic receiver and a thrown renderer fault see
            // the original stack, flags, registers, FPU and remaining quota.
            // The first checked write also invalidates any prior static page
            // signature. The following source packets overwrite these same
            // 256 bytes with the same sprite. The stack is non-executable,
            // the receiver is read-only, and all replay stores are outside
            // this slice. Rewriting it has no further memory side effect.
            apply(source,cpu,scope,sprite,n==0);
            cpu.r[wr::ESP]-=4;scope.store<32>(cpu.r[wr::ESP],returning);cpu.eip=0x4462e0;
            auto cmd=step.draw.command;
            cmd.tiles=std::span(step.draw.tiles).first(step.draw.size);
            if(!service_->submit_prepared(cmd,chunk.serial,n)){
                // Decline has no side effects. Let this source CALL select
                // its ordinary native or checked fallback on the live device.
                cpu.r[wr::ESP]+=4;cpu.eip=before;
                if(admitted)++text_page_replay_spans;
                return before;
            }
            if(remaining)--*remaining;
            count_page_glyph(step.draw.size,(cpu.fp.get16(0)&0x300)!=0);
            ++admitted;++text_page_replay_glyphs;
            cpu.r[wr::EAX]=0;cpu.r[wr::ESP]+=4;
            if(returning==0x447315)cpu.r[wr::EBX]=scope.load<32>(cpu.r[wr::ESP]+0x140);
            cpu.eip=0x4473d8;
        }
        ++text_page_replay_spans;return 0x4473d8;
    }
public:
    explicit TextPageReplay(wr::Memory& memory):memory_(memory){
        if(text_page_replay_enabled.load(std::memory_order_relaxed)){
            auto* service=SpriteService::find(memory);
            if(service && service->read_only_glyphs())service_=service;
        }
    }
    ~TextPageReplay(){cancel();}
    bool enabled()const{return service_!=nullptr;}
    bool recording()const{return recording_ && pending_;}
    void cancel(){
        if(recording_ && pending_)text_page_observer={};
        if(recording_ && recording_->count>=2 && !recording_->serial)recording_->serial=text_page::serial();
        pending_=false;recording_=nullptr;
    }
    Word begin(wr::Cpu& cpu,Scope& scope){
        using namespace text_page;
        finish_draw();
        // Each alignment is relative to the live byte cursor, not the guest
        // address. Control sequences and one-byte letters can split a group.
        if(!recording_ && ((cpu.r[wr::EBP]>>1)&(Width-1)))return 0;
        Key k;View sprite;Footprint guard;
        if(!inputs(cpu,scope,k,sprite) || !guard.prepare(scope,k.sp)){cancel();return 0;}
        if(recording_){
            const auto& first=recording_->steps[0].transition.key;
            if(k.sp!=first.sp || k.bp!=first.bp+2*recording_->count || k.local(0x20)!=first.local(0x20) ||
               k.arg(0x150)!=first.arg(0x150)){cancel();return 0;}
        }else{
            // Text and stack allocations are usually page-aligned. Mix the
            // high address bits and line origin before choosing a set.
            Word hash=(k.bp>>4)*0x9e3779b9u ^ k.local(0x20)*0x85ebca77u ^
                k.arg(0x158)*0xc2b2ae3du ^ k.sp*0x27d4eb2fu ^ k.arg(0x198);
            hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;
            auto& slot=chunks[hash&(chunks.size()-1)];
            for(auto& old:slot.ways){
                if(!old || !old->serial || !old->steps[0].transition.key.matches(k))continue;
                const unsigned n=matching_text(scope,guard,*old,k);
                if(n>=2 && resources(scope,guard,k,old->inputs,false)){
                    return replay(cpu,scope,*old,sprite,n);
                }
            }
            auto& dest=slot.ways[slot.next++%slot.ways.size()];
            if(!dest){
                const int saved=errno;dest.reset(new(std::nothrow) Chunk);errno=saved;
                if(!dest)return 0;
            }
            recording_=dest.get();recording_->count=0;recording_->serial=0;
        }
        auto& step=recording_->steps[recording_->count];
        step.transition.key=k;step.transition.end.pc=0;step.draw.captured=false;pending_=true;
        return 0;
    }
    void finish(wr::Cpu& cpu,Scope& scope,Word pc){
        using namespace text_page;
        if(!recording())return;
        auto& step=recording_->steps[recording_->count];const auto& key=step.transition.key;
        if(!capture_end(step.transition,cpu,scope,pc)){cancel();return;}
        auto& dep=recording_->inputs;
        const Word image=std::uint16_t(scope.load<16>(key.arg(0x150)+0x4a)+Key::word(step.transition.end.args.data()+32));
        if(!recording_->count){
            dep.image_number=image;
            Footprint guard;
            if(!guard.prepare(scope,key.sp) || !resources(scope,guard,key,dep,true)){cancel();return;}
        }else if(image!=dep.image_number){cancel();return;}
        // The complete source string must remain independent of the stack
        // writes even on the recording run. Do not keep an alias or pointer.
        Footprint guard;
        if(!guard.prepare(scope,key.sp) || !guard.independent(scope.readable(key.local(0x20)+key.bp+2,2))){cancel();return;}
        text_page_observer={&memory_,key.sp-4,pc==outline_call?0x447315u:0x4473d8u,&step.draw};
    }
};
}
