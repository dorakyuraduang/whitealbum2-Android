#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
namespace wa2::graphics {
inline bool texture_metadata_method(unsigned slot){return slot==3 || slot==7 || slot==8 || slot==10 || slot==12 || slot==13 || slot==17 || slot==18;}
inline bool surface_metadata_method(unsigned slot){return slot==3 || slot==7 || slot==8 || slot==10 || slot==11 || slot==12;}
inline bool batch_fixed_vertex_stage(bool programmable_vertex,unsigned primitive){return !programmable_vertex && primitive>=4 && primitive<=6;}
enum class SubmissionApply {full,textures,reuse};
inline bool update_shader_constants(std::span<std::uint8_t> target,std::span<const std::uint8_t> source){
    if(target.size()!=source.size())throw std::invalid_argument("shader constant byte count");
    if(source.empty() || !std::memcmp(target.data(),source.data(),source.size()))return false;
    std::memcpy(target.data(),source.data(),source.size());return true;
}
inline SubmissionApply submission_apply(bool valid,bool same_config,std::uint64_t word_changes,
    std::uint64_t texture_changes,bool fixed_pixel,std::uint16_t texture_mask,std::uint16_t previous_mask){
    if(!valid || !same_config || word_changes || (fixed_pixel && ((texture_mask^previous_mask)&255)))return SubmissionApply::full;
    return texture_changes?SubmissionApply::textures:SubmissionApply::reuse;
}
template<class Texture>std::uint16_t texture_mask(const std::array<Texture,16>& textures){
    std::uint16_t mask{};for(unsigned n=0;n<textures.size();++n)if(textures[n])mask|=std::uint16_t(1u<<n);return mask;
}
template<class Texture,class Bind,class Unbind>
void refresh_texture_bindings(const std::array<Texture,16>& textures,const std::array<Texture,16>& previous,Bind bind,Unbind unbind){
    // Sampler parameters belong to images, so an unchanged alias must also be
    // refreshed when another unit changes. Preserve ascending unit precedence.
    for(unsigned n=0;n<textures.size();++n)if(textures[n])bind(n,textures[n]);else if(previous[n])unbind(n);
}
inline bool cpu_only_device_method(unsigned slot){
    if((slot>=81 && slot<=84) || (slot>=94 && slot<=99) || (slot>=109 && slot<=114))return true;
    switch(slot){
        // CPU setters are applied at the next draw, after prior geometry.
        case 37:case 39:case 44:case 46:case 47:case 57:case 65:case 67:case 69:case 75:case 87:case 89:
        case 92:case 100:case 102:case 104:case 107:
        // Queries expose metadata, never pixels or GPU results. They must not
        // turn every GetTexture/AddRef pair into a separate driver draw.
        case 3:case 4:case 6:case 7:case 8:case 9:case 15:case 18:case 19:case 38:case 40:case 45:case 48:
        case 58:case 64:case 66:case 68:case 70:case 76:case 88:case 90:case 93:case 101:case 103:case 105:case 108:return true;
        default:return false;
    }
}
// These methods stay entirely within the owner thread's adapter. Display
// queries, window/cursor operations, Present, creation and reset remain outer
// boundaries. Draw staging may touch GL but never pumps events or guest code.
inline bool direct_device_method(unsigned slot){return cpu_only_device_method(slot) && slot!=8;}
// No GL operation, resource release or host query occurs in these handlers.
// In particular texture/shader/target setters are intentionally excluded:
// replacing their final reference can invoke a GL resource destructor.
inline bool context_free_device_method(unsigned slot){
    if((slot>=94 && slot<=99) || (slot>=109 && slot<=114))return true;
    switch(slot){
        case 44:case 45:case 46:case 47:case 48:case 57:case 58:
        case 66:case 67:case 68:case 69:case 75:case 76:case 90:case 102:case 103:return true;
        default:return false;
    }
}
}
