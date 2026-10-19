#pragma once
#include "fixed_pipeline.hpp"
#include <bit>
#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
namespace wa2::graphics {
// The common two-dimensional stream keeps exactly the shader inputs consumed
// by stages zero and one. Higher stages and texture transforms use the full
// renderer. Colours remain bytes until the GPU normalizes the attribute.
struct ScreenVertex {
    std::array<float,4> position;
    std::array<std::uint8_t,4> colour;
    std::array<std::array<float,2>,2> texture;
};
static_assert(sizeof(ScreenVertex)==36);
struct ScreenVertexPlan {
    abi::D3DVIEWPORT9 viewport;
    std::array<unsigned,2> coordinates;
    unsigned source_coordinates;
    unsigned source_prefix;
};
inline std::optional<ScreenVertexPlan> screen_vertex_plan(std::uint32_t fvf,
    const abi::D3DVIEWPORT9& viewport,std::array<unsigned,2> coordinates){
    using namespace abi;
    const auto format=fvf&~D3DFVF_SPECULAR;
    if((format!= (D3DFVF_XYZRHW|D3DFVF_DIFFUSE) &&
        format!=(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|(1u<<D3DFVF_TEXCOUNT_SHIFT)) &&
        format!=(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|(2u<<D3DFVF_TEXCOUNT_SHIFT))) ||
       !viewport.Width || !viewport.Height || coordinates[0]>=8 || coordinates[1]>=8)return {};
    return ScreenVertexPlan{viewport,coordinates,(fvf&D3DFVF_TEXCOUNT_MASK)>>D3DFVF_TEXCOUNT_SHIFT,
        fvf&D3DFVF_SPECULAR?24u:20u};
}
inline bool pack_screen_vertices(const ScreenVertexPlan& plan,std::span<const std::uint8_t> bytes,
    unsigned stride,std::span<ScreenVertex> output){
    if(!stride || plan.source_coordinates>2 || (plan.source_prefix!=20 && plan.source_prefix!=24) || stride<plan.source_prefix+plan.source_coordinates*8 || stride>256 || bytes.size()%stride || output.size()!=bytes.size()/stride)return false;
    // Common source sprites use XYZRHW + DIFFUSE + SPECULAR, even when the
    // secondary RGB contribution is zero. Retain the correct UV offset and
    // admit only zero secondary RGB; bright/specular effects keep the full path.
    if(plan.source_prefix==24)for(std::size_t n=0;n<output.size();++n){
        std::uint32_t specular;std::memcpy(&specular,bytes.data()+n*stride+20,4);if(specular&0xffffffu)return false;
    }
    for(std::size_t n=0;n<output.size();++n){
        const auto* source=bytes.data()+n*stride;auto& vertex=output[n];
        std::array<float,4> position;std::memcpy(position.data(),source,16);
        vertex.position=screen_position(position,plan.viewport);
        vertex.colour={source[18],source[17],source[16],source[19]};
        for(unsigned unit=0;unit<2;++unit){
            const auto coordinate=plan.coordinates[unit];
            if(coordinate<plan.source_coordinates)std::memcpy(vertex.texture[unit].data(),source+plan.source_prefix+coordinate*8,8);
            else vertex.texture[unit]={0,0};
        }
    }
    return true;
}
template<class Emit>inline void each_triangle(unsigned type,unsigned count,Emit emit){
    using namespace abi;
    if(type==D3DPT_TRIANGLELIST){for(unsigned n=0;n+2<count;n+=3)emit(n,n+1,n+2);}
    else if(type==D3DPT_TRIANGLESTRIP){for(unsigned n=0;n+2<count;++n)emit(n+(n&1),n+1-(n&1),n+2);}
    else if(type==D3DPT_TRIANGLEFAN){for(unsigned n=1;n+1<count;++n)emit(0,n,n+1);}
}
}
