#pragma once
#include "texture_batch.hpp"
#include <optional>
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace wa2::graphics {
// pack_screen_vertices() uses floating point in the renderer thread. Guest
// state alone cannot establish the rounding and denormal mode used there.
inline std::optional<unsigned> vertex_fp_mode(){
#if defined(__x86_64__) || defined(_M_X64)
    const unsigned value=_mm_getcsr();
    if((value&0x1f80u)!=0x1f80u)return {};
    return value&~0x3fu;
#elif defined(__aarch64__) && (defined(__clang__) || defined(__GNUC__))
    std::uint64_t value;
    __asm__("mrs %0, fpcr":"=r"(value));
    if(value&0x9f00u)return {};
    return unsigned(value);
#else
    return {};
#endif
}
struct GlyphGeometryRange {
    std::uint64_t serial{};
    unsigned index{},vertex{},element{},fp{};
    ScreenVertexPlan plan{};
    std::array<std::uint8_t,4> slots{};
    GeometryTicket ticket{};
    bool matches(std::uint64_t id,unsigned glyph,const ScreenVertexPlan& current,unsigned mode)const{
        return id && serial==id && index==glyph && fp==mode &&
            plan.viewport.X==current.viewport.X && plan.viewport.Y==current.viewport.Y &&
            plan.viewport.Width==current.viewport.Width && plan.viewport.Height==current.viewport.Height &&
            plan.coordinates==current.coordinates && plan.source_prefix==current.source_prefix &&
            plan.source_coordinates==current.source_coordinates;
    }
};
class PreparedGlyphGeometry {
    std::array<GlyphGeometryRange,1024> saved_{};
    std::array<GlyphGeometryRange,1024> pending_;
    unsigned pending_count_{};
    auto& entry(std::uint64_t serial,unsigned index){return saved_[((serial%128)*8)+index];}
public:
    const GlyphGeometryRange* find(std::uint64_t serial,unsigned index,const ScreenVertexPlan& plan,unsigned mode){
        if(index>=8)return nullptr;
        const auto& e=entry(serial,index);
        return e.matches(serial,index,plan,mode)?&e:nullptr;
    }
    void record(GlyphGeometryRange range){
        if(range.serial && range.index<8 && pending_count_<pending_.size())pending_[pending_count_++]=range;
    }
    void finish(GeometryTicket ticket){
        if(ticket.generation)for(auto& r:std::span(pending_).first(pending_count_)){r.ticket=ticket;entry(r.serial,r.index)=r;}
        pending_count_=0;
    }
};
}
