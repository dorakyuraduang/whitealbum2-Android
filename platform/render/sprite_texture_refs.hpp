#pragma once
#include <array>
#include <span>
#include <stdexcept>
#include <variant>
namespace wa2::graphics {
// A glyph usually needs one to four tiles. Keep their strong references on
// the stack without constructing/destructing 256 null references per glyph.
// Larger sprite packets retain the same 256-tile bound and call-local lifetime.
template<class Texture> class SpriteTextureRefs {
    std::variant<std::array<Texture,4>,std::array<Texture,256>> references_;
    std::span<Texture> values_;
public:
    static constexpr unsigned InlineCapacity=4,MaximumCapacity=256;
    explicit SpriteTextureRefs(std::size_t count){
        if(count>MaximumCapacity)throw std::length_error("Native sprite texture count exceeds its bound");
        if(count>InlineCapacity)values_=references_.template emplace<1>();
        else values_=std::get<0>(references_);
        values_=values_.first(count);
    }
    SpriteTextureRefs(const SpriteTextureRefs&)=delete;
    SpriteTextureRefs& operator=(const SpriteTextureRefs&)=delete;
    SpriteTextureRefs(SpriteTextureRefs&&)=delete;
    SpriteTextureRefs& operator=(SpriteTextureRefs&&)=delete;
    Texture& operator[](std::size_t index){return values_[index];}
    std::size_t size()const{return values_.size();}
};
}
