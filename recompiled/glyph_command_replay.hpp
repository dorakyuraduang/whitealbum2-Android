#pragma once
#include "glyph_command_replay_control.hpp"
#include "native_sprites.hpp"
#include "../engine/byte_equality.hpp"
#include "../engine/glyph_index.hpp"
#include <winrecomp/runtime.hpp>
#include <cerrno>
#include <cstring>
#include <memory>
#include <new>

namespace wa2::aot {
class GlyphCommandReplay {
    using Word = wr::U32;
    using Scope = wr::Memory::ExecutionScope;
    using Sprite = std::array<std::uint8_t,256>;
    using Args = std::array<Word,9>;
    using Tiles = std::array<SpriteTile,4>;
    struct Resources {
        std::array<std::uint8_t,128> image;
        Word index, palette, cell, half, active, device, target, linear, caps, separate, source_entry;
        bool operator==(const Resources& other) const {
            return source_entry==other.source_entry && index==other.index && palette==other.palette && cell==other.cell &&
                   half==other.half && active==other.active && device==other.device &&
                   target==other.target && linear==other.linear && caps==other.caps &&
                   separate==other.separate &&
                   wa2::memory::equal_bytes<128>(image.data(),other.image.data());
        }
    };
    struct Atlas {
        std::array<Word,4> indices{}, handles{};
        unsigned size{};
    };
public:
    struct Entry {
        Sprite sprite;
        Args args;
        Resources resources;
        Atlas atlas;
        SpriteCommand command{};
        Tiles tiles;
        Word fingerprint{}, control{}, tile_count{};
        bool ready{};
    };
private:
    static constexpr unsigned sets=256, ways=4;
    struct Set { std::array<Entry,ways> entries; unsigned next{}; };
    std::unique_ptr<std::array<Set,sets>> table_;
    // First visits to new positions (notably fast-forward) run the stock path.
    // Fingerprint collisions here only affect when an entry may be recorded.
    std::array<Word,2048> seen_{};
    template<class T> static T word(const std::uint8_t* data,unsigned offset) {
        T result;std::memcpy(&result,data+offset,sizeof(result));return result;
    }
    static Word fingerprint(const Sprite& s,const Args& a,Word control) {
        Word h = a[0]*0x9e3779b9u ^ std::rotl(a[1]*0x85ebca77u,11) ^
                 std::rotl(a[2]*0xc2b2ae3du,17) ^ std::rotl(a[7],13) ^
                 a[8]*0x27d4eb2fu ^ word<Word>(s.data(),0x48) ^
                 word<Word>(s.data(),0x88) ^ std::rotl(word<Word>(s.data(),0x98),7) ^ control;
        h^=h>>16;h*=0x7feb352du;h^=h>>15;
        return h ? h : 1;
    }
    static bool current(wr::Memory& memory,Scope& scope,wa2::text::GlyphIndex& mapping,
                        const SpriteSource& source,const Entry& entry,Resources& r) {
        r.source_entry=source.glyph_entry;
        const auto& s=entry.sprite;
        const auto& a=entry.args;
        const unsigned image=std::uint16_t(word<std::uint16_t>(s.data(),0x4a)+a[8]);
        if(image>=1024)return false;
        auto code=std::uint16_t(a[0]);
        if(std::uint16_t(code+0x4f)>0x2c && code>255)code=std::uint16_t((code<<8)|(code>>8));
        const auto ranges=scope.readable(source.font_ranges,256);
        if(ranges.size()!=256)return false;
        const auto index=mapping.lookup(std::span<const std::uint8_t,256>{ranges.data(),256},code);
        if(!index || *index>0x7fffffffu)return false;
        r.index=*index;
        const auto globals=scope.readable(source.globals,0x440);
        // Special's globals straddle a backing-page boundary. Keep the main
        // profile's single-view path; crossings read the same live fields
        // through the already-held checked scope instead of losing reuse.
        const auto global=[&](unsigned at){return globals.size()==0x440?
            word<Word>(globals.data(),at):scope.load<32>(source.globals+at);};
        r.active=global(4);r.device=global(0x438);r.linear=global(0x42c);
        r.caps=global(0x2c0);r.separate=global(0x2a0);
        r.target=global(global(0x428) ? 0x43c : 0);
        if(!r.active)return false;
        const int style=word<std::int16_t>(s.data(),0x50);
        const int palette=std::bit_cast<std::int8_t>(s[0x52]);
        r.cell=scope.load<8>(Word(source.font_styles+style*0x34));
        r.palette=scope.load<32>(Word(source.palette+palette*4));
        r.half=scope.load<32>(source.half_pixel);
        const auto address=source.images+image*128;
        const auto view=scope.readable(address,128);
        if(view.size()==128)std::memcpy(r.image.data(),view.data(),128);
        else memory.copy_out(address,r.image);
        return true;
    }
    // The UVs used by both original builders are whole glyph-cell coordinates
    // followed by an optional half scale. At these bounds every step is exact
    // even at PC24. Capture ALL intersected atlas slots, including zero handles.
    static bool covered(const Args& a,const Resources& r,Atlas& atlas) {
        const auto offset=std::bit_cast<std::int32_t>(a[5]);
        const auto size=std::bit_cast<std::int32_t>(a[6]);
        if(offset<0 || offset>32768 || size<=0 || size>32768)return false;
        const int cell=std::bit_cast<std::int32_t>(a[8])<2 ? 40 : 24;
        const std::int64_t u=std::int64_t(r.index%80)*cell+offset;
        const std::int64_t v=std::int64_t(r.index/80)*cell+offset;
        const auto dim=[&](unsigned at){return int(word<std::int16_t>(r.image.data(),at));};
        const int scale=r.image[4]?2:1;
        const int width=dim(0x5c)*scale,height=dim(0x5e)*scale;
        const int tw=dim(0x60)*scale,th=dim(0x62)*scale,nx=dim(0x64),ny=dim(0x66);
        if(tw<=0 || th<=0 || nx<1 || nx>16 || ny<1 || ny>16 ||
           u+size>32768 || v+size>32768 || u+size>width || v+size>height)return false;
        const int left=int(u)/tw,right=int(u+size-1)/tw,top=int(v)/th,bottom=int(v+size-1)/th;
        if(right>=nx || bottom>=ny || right-left>1 || bottom-top>1)return false;
        atlas.size=0;
        for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x)
            atlas.indices[atlas.size++]=Word(y*nx+x);
        return true;
    }
    static bool handles(wr::Memory& memory,Scope& scope,const Resources& r,const Atlas& a,
                        std::array<Word,4>& values) {
        const auto pointer=word<Word>(r.image.data(),0x70);
        const auto nx=word<std::int16_t>(r.image.data(),0x64),ny=word<std::int16_t>(r.image.data(),0x66);
        const auto bytes=unsigned(nx*ny)*4;
        if(std::uint64_t(pointer)+bytes>0x100000000ull)return false;
        const auto view=scope.readable(pointer,bytes);
        if(view.size()==bytes){
            for(unsigned n=0;n<a.size;++n)values[n]=word<Word>(view.data(),a.indices[n]*4);
        }else{
            // The original builder checks the entire handle array, including
            // inaccessible unused entries, on both its scalar and grid paths.
            memory.check(pointer,bytes,wr::Memory::Read);
            for(unsigned n=0;n<a.size;++n)values[n]=scope.load<32>(pointer+a.indices[n]*4);
        }
        return true;
    }
public:
    // A null ticket after a miss lets one-use glyphs bypass the capture cost.
    // The source still submits through the live SpriteService on every hit.
    bool find(wr::Memory& memory,Scope& scope,wa2::text::GlyphIndex& mapping,
              const SpriteSource& source,const Sprite& sprite,const Args& args,Word control,SpriteCommand& out,
              Tiles& tiles,Entry*& ticket) {
        if(word<std::int16_t>(sprite.data(),0x48)>=0 || word<Word>(sprite.data(),0x74))return false;
        const auto hash=fingerprint(sprite,args,control);
        auto& seen=seen_[hash & (seen_.size()-1)];
        const bool repeated=seen==hash;
        seen=hash;
        if(!table_){
            if(!repeated)return false;
            const auto saved_errno=errno;
            table_.reset(new(std::nothrow) std::array<Set,sets>{});
            errno=saved_errno;
            if(!table_)return false;
        }
        auto& set=(*table_)[hash&(sets-1)];
        for(auto& e:set.entries){
            if(e.fingerprint!=hash || e.control!=control || e.args!=args ||
               !wa2::memory::equal_bytes<256>(e.sprite.data(),sprite.data()))continue;
            ticket=&e;
            Resources current_state;
            std::array<Word,4> live_handles{};
            if(!e.ready || !current(memory,scope,mapping,source,e,current_state) ||
               !(current_state==e.resources) || !handles(memory,scope,current_state,e.atlas,live_handles) ||
               live_handles!=e.atlas.handles)return false;
            out=e.command;
            std::copy_n(e.tiles.data(),e.tile_count,tiles.data());
            out.tiles=std::span(tiles).first(e.tile_count);
            ++glyph_command_replay_hits;ticket=nullptr;return true;
        }
        if(repeated){
            auto& e=set.entries[set.next++%ways];
            e.ready=false;e.fingerprint=hash;e.control=control;e.args=args;e.sprite=sprite;ticket=&e;
        }
        return false;
    }
    void capture(wr::Memory& memory,Scope& scope,wa2::text::GlyphIndex& mapping,
                 const SpriteSource& source,Entry& e,const SpriteCommand& command) {
        e.ready=false;
        if(command.target_kind!=SpriteTarget::surface || command.tiles.size()>e.tiles.size() ||
           !current(memory,scope,mapping,source,e,e.resources) || !covered(e.args,e.resources,e.atlas))return;
        e.atlas.handles.fill(0);
        if(!handles(memory,scope,e.resources,e.atlas,e.atlas.handles))return;
        e.command=command;e.command.tiles={};
        e.tile_count=unsigned(command.tiles.size());
        std::copy_n(command.tiles.begin(),e.tile_count,e.tiles.begin());
        e.ready=true;++glyph_command_replay_captures;
    }
};
}
