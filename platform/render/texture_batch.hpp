#pragma once
#include "screen_vertices.hpp"
#include "immutable_buffers.hpp"
#include <algorithm>
#include <stdexcept>
#include <vector>
namespace wa2::graphics {
inline constexpr unsigned TextureBatchSlots=8;
inline constexpr unsigned WideTextureBatchSlots=16;
inline unsigned supported_texture_batch_slots(int fragment_units,bool gradients){
    if(!gradients || fragment_units<int(TextureBatchSlots))return 0;
    return fragment_units>=int(WideTextureBatchSlots)?WideTextureBatchSlots:TextureBatchSlots;
}
template<class Applied,class State>
bool same_glyph_packet_state(const Applied& previous,const State& next,bool same_config,
                            std::uint64_t word_differences){
    // The config tracks target/viewport, depth, transforms, declarations,
    // shaders and constants. Render, stage and sampler words track net edits.
    // Stream/index owners and transform insertion by a query are unversioned.
    return same_config && !word_differences && previous.target==next.target &&
        previous.scene==next.scene && previous.indices==next.indices && previous.streams==next.streams &&
        previous.transform_count==next.transforms.size() && previous.textures[0] && next.textures[0] &&
        std::equal(previous.textures.begin()+1,previous.textures.end(),next.textures.begin()+1);
}
template<class Textures, class Apply>
void apply_logical_texture_units(const Textures& textures, unsigned deferred_glyph_slots, Apply apply) {
    for (unsigned unit = 0; unit < textures.size(); ++unit) {
        if (unit > 0 && unit < deferred_glyph_slots && !textures[unit]) continue;
        apply(unit, textures[unit]);
    }
}

// A preserved rollover immediately follows a real draw with the same fixed
// shader, packet width, target and GL context. The draw only touched its
// packet units. Its higher logical units were already empty. Unit zero must
// still upload and bind the incoming texture and validate its sampler.
template<class Textures,class Select,class BindSampler,class Clear>
unsigned bind_logical_texture_units(const Textures& textures,unsigned slots,bool preserved,
                                    Select select,BindSampler bind_sampler,Clear clear){
    if(preserved && slots && slots<=textures.size() && textures[0] &&
       std::none_of(textures.begin()+1,textures.end(),[](const auto& texture){return bool(texture);})){
        bind_sampler(0,textures[0]); // also selects unit zero, leaving it active
        // Two selections around the logical sampler loop, plus a selection
        // and an unbind for each untouched unit above this packet's width.
        return 2u+2u*(unsigned(textures.size())-slots);
    }
    apply_logical_texture_units(textures,slots,[&](unsigned n,const auto& texture){
        select(n);
        if(texture)bind_sampler(n,texture);else clear();
    });
    select(0);
    return 0;
}
struct TextureBatchVertex {ScreenVertex screen;float binding;};
static_assert(sizeof(TextureBatchVertex)==40);
// The adapter uses this same bounded packet builder on every host. Textures
// stay separate images: no atlas, resampling, resource copy or UV remapping.
template<class Texture>class ScreenTextureBatch {
    std::array<Texture,WideTextureBatchSlots> bindings_{};
    unsigned limit_{TextureBatchSlots};
    unsigned used_{};
    mutable std::vector<TextureBatchVertex> vertices_;
    mutable std::vector<std::uint16_t> indices_;
    mutable GeometryBytes borrowed_;
    mutable GeometryTicket ticket_{};
    unsigned borrowed_vertices_{},borrowed_indices_{};
public:
    static constexpr unsigned MaxVertices=4096,MaxIndices=12288;
    unsigned slot(const Texture& texture)const{
        for(unsigned n=0;n<used_;++n)if(bindings_[n]==texture)return n;
        return used_;
    }
    bool empty()const{return element_count()==0;}
    unsigned vertex_count()const{return ticket_.generation?borrowed_vertices_:unsigned(vertices_.size());}
    unsigned element_count()const{return ticket_.generation?borrowed_indices_:unsigned(indices_.size());}
    GeometryTicket geometry_ticket()const{return ticket_;}
    // Only one packet is pending on a graphics device. The immutable cache can
    // evict GPU storage only when that packet is drained and no borrow survives.
    bool borrow(GeometryTicket ticket,GeometryBytes bytes,unsigned vertex_base,unsigned index_base,
                std::span<const Texture> textures,std::span<const std::uint8_t> slots){
        if(!ticket.generation || textures.empty() || textures.size()>4 || slots.size()!=textures.size() ||
           vertex_count()!=vertex_base || element_count()!=index_base ||
           (ticket_.generation && ticket_!=ticket))return false;
        const unsigned end_vertex=vertex_base+4*unsigned(textures.size()),end_index=index_base+6*unsigned(textures.size());
        if(end_vertex>MaxVertices || end_index>MaxIndices || end_vertex*sizeof(TextureBatchVertex)>bytes.vertices.size() ||
           end_index*sizeof(std::uint16_t)>bytes.indices.size())return false;
        if(!ticket_.generation && ((vertex_base && std::memcmp(vertices_.data(),bytes.vertices.data(),vertex_base*sizeof(TextureBatchVertex))) ||
                                   (index_base && std::memcmp(indices_.data(),bytes.indices.data(),index_base*sizeof(std::uint16_t)))))return false;
        unsigned next=used_;
        for(unsigned i=0;i<textures.size();++i){
            if(!textures[i])return false;
            unsigned binding=slot(textures[i]);
            if(binding==used_){
                unsigned j=0;
                while(j<i && textures[j]!=textures[i])++j;
                binding=j<i?slots[j]:next++;
            }
            if(binding!=slots[i] || binding>=limit_)return false;
        }
        for(unsigned i=0;i<textures.size();++i)if(slots[i]==used_)bindings_[used_++]=textures[i];
        ticket_=ticket;borrowed_=bytes;borrowed_vertices_=end_vertex;borrowed_indices_=end_index;
        return true;
    }
    void materialize()const{
        if(!ticket_.generation)return;
        vertices_.resize(borrowed_vertices_);indices_.resize(borrowed_indices_);
        std::memcpy(vertices_.data(),borrowed_.vertices.data(),vertices_.size()*sizeof(TextureBatchVertex));
        std::memcpy(indices_.data(),borrowed_.indices.data(),indices_.size()*sizeof(std::uint16_t));
        ticket_={};borrowed_={};
    }
    unsigned binding_count()const{return used_;}
    unsigned slot_limit()const{return limit_;}
    void set_slot_limit(unsigned slots){
        if(!empty() || (slots!=TextureBatchSlots && slots!=WideTextureBatchSlots))
            throw std::logic_error("invalid texture packet capacity change");
        limit_=slots;
    }
    // Grow only after a real glyph run fills its sampler table. Small or
    // unrelated draws keep the narrower shader on large and small GPUs alike.
    unsigned slots_after_glyph_rollover(const Texture& texture,unsigned supported)const{
        return supported==WideTextureBatchSlots && used_==limit_ && slot(texture)==limit_
            ?WideTextureBatchSlots:TextureBatchSlots;
    }
    const auto& bindings()const{return bindings_;}
    std::span<const TextureBatchVertex> vertices()const{materialize();return vertices_;}
    std::span<const std::uint16_t> indices()const{materialize();return indices_;}
    static unsigned index_count(unsigned type,unsigned count){
        return type==abi::D3DPT_TRIANGLELIST?count/3*3:
            (type==abi::D3DPT_TRIANGLESTRIP || type==abi::D3DPT_TRIANGLEFAN) && count>2?(count-2)*3:0;
    }
    bool fits(const Texture& texture,unsigned vertices,unsigned indices)const{
        return bool(texture) && slot(texture)<limit_ &&
            vertices<=MaxVertices-vertex_count() && indices<=MaxIndices-element_count();
    }
    // Only called after the incoming draw failed to fit. The older packet
    // must really draw, with the same program width as the next valid one.
    bool can_roll_glyph_packet(bool previous_glyph,bool next_glyph,unsigned next_slots,
                               std::size_t vertices,std::size_t indices)const{
        return !empty() && previous_glyph && next_glyph && next_slots==limit_ &&
            vertices>0 && vertices<=MaxVertices && indices>0 && indices<=MaxIndices;
    }
    bool append_glyph_quad(const Texture& texture, const ScreenVertexPlan& plan,
                           std::span<const std::uint8_t> bytes) {
        if (!texture || vertex_count() + 4 > MaxVertices || element_count() + 6 > MaxIndices)
            return false;
        const auto binding = slot(texture);
        if (binding == limit_) return false;
        std::array<ScreenVertex, 4> quad;
        if (!pack_screen_vertices(plan, bytes, 32, quad)) return false;

        materialize();
        if (binding == used_) bindings_[used_++] = texture;
        const auto base = static_cast<std::uint16_t>(vertices_.size());
        for (const auto& vertex : quad) vertices_.push_back({vertex, float(binding)});
        const auto next = static_cast<std::uint16_t>(base + 1);
        const auto third = static_cast<std::uint16_t>(base + 2);
        indices_.insert(indices_.end(), {base, next, third, third, next, static_cast<std::uint16_t>(base + 3)});
        return true;
    }
    bool append(const Texture& texture,std::span<const ScreenVertex> vertices,unsigned type,
                unsigned count,std::span<const std::uint32_t> indices={}){
        if(count>MaxIndices || vertices.size()>MaxVertices || indices.size()>MaxIndices)return false;
        const auto elements=index_count(type,count);
        if(!elements || (indices.empty()?count>vertices.size():indices.size()!=count) ||
           !fits(texture,unsigned(vertices.size()),elements))return false;
        for(auto index:indices)if(index>=vertices.size())return false;
        materialize();
        const auto binding=slot(texture),base=unsigned(vertices_.size());
        if(binding==used_)bindings_[used_++]=texture;
        for(const auto& vertex:vertices)vertices_.push_back({vertex,float(binding)});
        const auto index=[&](unsigned n){return std::uint16_t(base+(indices.empty()?n:indices[n]));};
        each_triangle(type,count,[&](unsigned a,unsigned b,unsigned c){
            indices_.insert(indices_.end(),{index(a),index(b),index(c)});
        });
        return true;
    }
    void clear(){ticket_={};borrowed_={};vertices_.clear();indices_.clear();bindings_.fill(Texture{});used_=0;}
};
}
