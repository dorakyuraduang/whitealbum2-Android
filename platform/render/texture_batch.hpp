#pragma once
#include "screen_vertices.hpp"
#include <algorithm>
#include <vector>
namespace wa2::graphics {
inline constexpr unsigned TextureBatchSlots=8;
struct TextureBatchVertex {ScreenVertex screen;float binding;};
static_assert(sizeof(TextureBatchVertex)==40);
// The adapter uses this same bounded packet builder on every host. Textures
// stay separate images: no atlas, resampling, resource copy or UV remapping.
template<class Texture>class ScreenTextureBatch {
    std::array<Texture,TextureBatchSlots> bindings_{};
    unsigned used_{};
    std::vector<TextureBatchVertex> vertices_;
    std::vector<std::uint16_t> indices_;
public:
    static constexpr unsigned MaxVertices=4096,MaxIndices=12288;
    unsigned slot(const Texture& texture)const{
        for(unsigned n=0;n<used_;++n)if(bindings_[n]==texture)return n;
        return used_;
    }
    bool empty()const{return indices_.empty();}
    unsigned binding_count()const{return used_;}
    const auto& bindings()const{return bindings_;}
    std::span<const TextureBatchVertex> vertices()const{return vertices_;}
    std::span<const std::uint16_t> indices()const{return indices_;}
    static unsigned index_count(unsigned type,unsigned count){
        return type==abi::D3DPT_TRIANGLELIST?count/3*3:
            (type==abi::D3DPT_TRIANGLESTRIP || type==abi::D3DPT_TRIANGLEFAN) && count>2?(count-2)*3:0;
    }
    bool fits(const Texture& texture,unsigned vertices,unsigned indices)const{
        return bool(texture) && slot(texture)<TextureBatchSlots &&
            vertices<=MaxVertices-vertices_.size() && indices<=MaxIndices-indices_.size();
    }
    bool append(const Texture& texture,std::span<const ScreenVertex> vertices,unsigned type,
                unsigned count,std::span<const std::uint32_t> indices={}){
        if(count>MaxIndices || vertices.size()>MaxVertices || indices.size()>MaxIndices)return false;
        const auto elements=index_count(type,count);
        if(!elements || (indices.empty()?count>vertices.size():indices.size()!=count) ||
           !fits(texture,unsigned(vertices.size()),elements))return false;
        for(auto index:indices)if(index>=vertices.size())return false;
        const auto binding=slot(texture),base=unsigned(vertices_.size());
        if(binding==used_)bindings_[used_++]=texture;
        for(const auto& vertex:vertices)vertices_.push_back({vertex,float(binding)});
        const auto index=[&](unsigned n){return std::uint16_t(base+(indices.empty()?n:indices[n]));};
        each_triangle(type,count,[&](unsigned a,unsigned b,unsigned c){
            indices_.insert(indices_.end(),{index(a),index(b),index(c)});
        });
        return true;
    }
    void clear(){vertices_.clear();indices_.clear();bindings_.fill(Texture{});used_=0;}
};
}
