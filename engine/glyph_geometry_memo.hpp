#pragma once
#include "byte_equality.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <type_traits>

namespace wa2::text {
// Exact memo of a glyph's pure tile geometry. Resource/renderer state is still
// resolved by the caller. Keys own all bytes; no guest pointer is retained.
template<class Tile> class GlyphGeometryMemo {
    static_assert(std::is_trivially_copyable_v<Tile>);
public:
#if defined(WA2_AOT_DIAGNOSTICS) && WA2_AOT_DIAGNOSTICS
    struct Stats {std::uint64_t finds{},hits{},first_uses{},captures{};};
    inline static thread_local Stats stats;
    inline static thread_local bool enabled=true;
#endif
    struct Input {
        std::span<const std::uint8_t,256> sprite;
        std::span<const std::uint8_t,128> image;
        std::span<const std::uint8_t,4> palette;
        std::uint32_t half;
        std::span<const std::uint32_t> handles;
    };
    static constexpr unsigned Slots=128,MaximumTiles=4,MaximumHandles=256;
private:
    struct Entry {
        std::array<std::uint8_t,256> sprite;
        std::array<std::uint8_t,128> image;
        std::array<std::uint8_t,4> palette;
        std::array<std::uint32_t,MaximumHandles> handles;
        std::array<Tile,MaximumTiles> tiles;
        std::uint32_t half{},handle_count{},tile_count{};
        bool valid{},seen{};
    };
    std::unique_ptr<Entry[]> entries_;
    static unsigned slot(const Input& in){
        std::uint32_t hash=2166136261u;
        for(unsigned at:{0x88u,0x98u,0xbcu,0xc4u,0x63u,0x48u}){std::uint32_t word;std::memcpy(&word,in.sprite.data()+at,4);hash=(hash^word)*16777619u;}
        // Pixel coordinates and UVs have many zero low bits. Avalanche the
        // complete words before selecting a slot, rather than preserving their
        // regular grid spacing as systematic cache collisions.
        hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;hash*=0x846ca68bu;hash^=hash>>16;
        return hash&(Slots-1);
    }
    static bool matches(const Entry& e,const Input& in){
        return e.valid && e.handle_count==in.handles.size() && e.half==in.half &&
            wa2::memory::equal_bytes<256>(e.sprite.data(),in.sprite.data()) &&
            wa2::memory::equal_bytes<128>(e.image.data(),in.image.data()) &&
            wa2::memory::equal_bytes<4>(e.palette.data(),in.palette.data()) &&
            wa2::memory::equal_words(std::span(e.handles).first(e.handle_count),in.handles);
    }
public:
    static constexpr unsigned MaximumHeapBytes=Slots*sizeof(Entry);
    std::optional<std::span<const Tile>> find(const Input& in)const{
#if defined(WA2_AOT_DIAGNOSTICS) && WA2_AOT_DIAGNOSTICS
        if(!enabled)return {};
        ++stats.finds;
#endif
        if(!entries_ || in.handles.size()>MaximumHandles)return {};
        const auto& entry=entries_[slot(in)];if(!matches(entry,in))return {};
#if defined(WA2_AOT_DIAGNOSTICS) && WA2_AOT_DIAGNOSTICS
        ++stats.hits;
#endif
        return std::span(entry.tiles).first(entry.tile_count);
    }
    void remember(const Input& in,std::span<const Tile> tiles){
#if defined(WA2_AOT_DIAGNOSTICS) && WA2_AOT_DIAGNOSTICS
        if(!enabled)return;
#endif
        if(tiles.size()>MaximumTiles || in.handles.size()>MaximumHandles)return;
        if(!entries_){entries_.reset(new(std::nothrow) Entry[Slots]);if(!entries_)return;}
        auto& entry=entries_[slot(in)];
        // Fast-forward introduces new glyphs continuously. Only a repeated
        // sprite earns the complete metadata/tile capture; first-use keys cost
        // one bounded 256-byte copy and retain the ordinary calculation.
        if(!entry.seen || !wa2::memory::equal_bytes<256>(entry.sprite.data(),in.sprite.data())){
#if defined(WA2_AOT_DIAGNOSTICS) && WA2_AOT_DIAGNOSTICS
            ++stats.first_uses;
#endif
            entry.valid=false;std::memcpy(entry.sprite.data(),in.sprite.data(),in.sprite.size());entry.seen=true;return;
        }
        entry.valid=false;std::memcpy(entry.image.data(),in.image.data(),in.image.size());
        std::memcpy(entry.palette.data(),in.palette.data(),in.palette.size());entry.half=in.half;
        if(!in.handles.empty())std::memcpy(entry.handles.data(),in.handles.data(),in.handles.size_bytes());
        if(!tiles.empty())std::memcpy(entry.tiles.data(),tiles.data(),tiles.size_bytes());
        entry.handle_count=unsigned(in.handles.size());entry.tile_count=unsigned(tiles.size());entry.valid=true;
#if defined(WA2_AOT_DIAGNOSTICS) && WA2_AOT_DIAGNOSTICS
        ++stats.captures;
#endif
    }
};
}
