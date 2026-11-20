#pragma once
#include "texture_batch.hpp"
#include "../../recompiled/native_sprites.hpp"
#include <memory>

namespace wa2::graphics {
// Valid only between two consecutive surface glyph submissions. Any adapter
// entry, flushed packet or unsupported draw must end the proof.
struct GlyphCommandState {
    std::array<std::uint32_t,10> words{};
    bool valid{};
    static auto inputs(const aot::SpriteCommand& c){
        return std::array{c.device,c.target,c.blend_enabled,c.blend_op,c.source_blend,
            c.destination_blend,c.minimum,c.magnification,c.mipmap,c.alpha_argument};
    }
    void capture(const aot::SpriteCommand& c){
        valid=c.glyph && c.target_kind==aot::SpriteTarget::surface && !c.tiles.empty();
        if(valid)words=inputs(c);
    }
    bool matches(const aot::SpriteCommand& c)const{
        return valid && c.glyph && c.target_kind==aot::SpriteTarget::surface &&
            !c.tiles.empty() && c.tiles.size()<=4 && words==inputs(c);
    }
};

// Particle vertices change every frame. Only the last accepted packet's
// device, target, raster words and vertex layout are reusable.
struct ParticleCommandState {
    decltype(GlyphCommandState::words) words{};
    aot::SpriteTarget target_kind{};
    std::uint32_t surface_result{};
    ScreenVertexPlan plan{};
    bool valid{};
    void capture(const aot::SpriteCommand& command,const ScreenVertexPlan& layout){
        words=GlyphCommandState::inputs(command);target_kind=command.target_kind;surface_result=command.surface_result;plan=layout;valid=true;
    }
    bool matches(const aot::SpriteCommand& command)const{
        return valid && command.particle && !command.glyph && command.tiles.size()==1 &&
            command.target_kind==target_kind && command.surface_result==surface_result && GlyphCommandState::inputs(command)==words;
    }
};

// Final guest Release clears interface_handle even if an older GPU packet
// keeps the Resource alive. A recycled token must resolve through the map.
template<class Resource>class GlyphTextureLookup {
    std::array<std::weak_ptr<Resource>,128> entries_;
public:
    template<class Lookup>std::shared_ptr<Resource> resolve(std::uint32_t token,Lookup lookup){
        auto& entry=entries_[(token>>2)%entries_.size()];
        if(auto resource=entry.lock();resource && token && resource->interface_handle==token)return resource;
        auto resource=lookup(token);entry=resource;return resource;
    }
};

// Reserve and validate the complete command before appending its first tile.
// This lets a split glyph retry unchanged on the ordinary renderer path.
template<class Texture>
bool append_surface_glyph(ScreenTextureBatch<Texture>& batch,const ScreenVertexPlan& plan,
                          std::span<const aot::SpriteTile> tiles,std::span<const Texture> textures){
    if(tiles.empty() || tiles.size()>4 || tiles.size()!=textures.size() ||
       tiles.size()*4>batch.MaxVertices-batch.vertex_count() ||
       tiles.size()*6>batch.MaxIndices-batch.element_count())return false;
    if(tiles.size()==1){
        const auto& vertices=tiles.front().vertices;
        return batch.append_glyph_quad(textures.front(),plan,
            {reinterpret_cast<const std::uint8_t*>(vertices.data()),sizeof(vertices)});
    }
    unsigned bindings=batch.binding_count();
    std::array<std::array<ScreenVertex,4>,4> packed;
    for(std::size_t n=0;n<tiles.size();++n){
        if(!textures[n])return false;
        if(batch.slot(textures[n])==batch.binding_count() &&
           std::find(textures.begin(),textures.begin()+n,textures[n])==textures.begin()+n &&
           ++bindings>batch.slot_limit())return false;
        const auto& vertices=tiles[n].vertices;
        if(!pack_screen_vertices(plan,{reinterpret_cast<const std::uint8_t*>(vertices.data()),sizeof(vertices)},
                                 sizeof(aot::SpriteVertex),packed[n]))return false;
    }
    for(std::size_t n=0;n<tiles.size();++n)
        if(!batch.append(textures[n],packed[n],abi::D3DPT_TRIANGLESTRIP,4))
            throw std::logic_error("prevalidated surface glyph did not fit its packet");
    return true;
}
}
