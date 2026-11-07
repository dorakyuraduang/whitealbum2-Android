#pragma once
#include "scene_sprite.hpp"
#include "scene_music.hpp"
#include "scene_effects.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <stdexcept>

namespace wa2::game {
// Content order shared by the music browser and original small BGM label.
// Metadata from the existing cache; no source address enters game state.
inline constexpr std::array<int,63> music_songs{4,1,2,3,5,6,18,7,8,9,10,11,12,13,14,15,16,17,27,33,32,28,19,20,21,26,29,23,24,22,34,60,31,40,41,42,43,44,45,46,47,69,74,52,53,61,50,51,62,63,67,68,70,71,54,55,64,58,59,66,56,57,65};
struct MusicCaption {
    std::int32_t song{-1};audio::Levels levels{};
    MusicCaption(std::int32_t id=-1,audio::Levels value={}):song(id),levels(value){}
};
inline MusicCaption music_caption_state(const script::SceneMusic& music,const script::SceneEffects& effects,bool paused){
    return {music.song(),paused?audio::Levels{}:music.song()>=0?music.levels():effects.levels()};
}
using CurrentMusic=std::function<MusicCaption()>;
inline void append_music_caption(std::vector<image::Sprite>& out,const std::shared_ptr<const image::Raster>& marker,
        const std::shared_ptr<const image::Raster>& labels,MusicCaption state,int layer,int& order){
    const auto song=state.song;if(song< -1 || song>=200)throw std::invalid_argument("Native music caption song is out of range");
    if(state.levels[0]>32768 || state.levels[1]>32768)throw std::invalid_argument("Native music caption sample level is out of range");
    if(!marker || !labels || marker->width<192 || marker->height<8 || labels->width<264 || labels->height<1008)
        throw std::runtime_error("Native music caption atlas dimensions are not supported");
    // Cached 4121e9/41ed80/422ef0 use the sum of the two raw mean levels,
    // divided by 200 in four-pixel steps. Clip saturated bars to their atlas.
    const auto width=std::min(192u,(state.levels[0]+state.levels[1])/200u*4u);
    const auto marker_order=order++,label_order=order++;
    if(width){out.push_back({marker,24,688,float(width),8,0,0,float(width),8});out.back().layer=layer;out.back().order=marker_order;}
    if(song==-1)return;
    for(unsigned row=0;row<music_songs.size();++row)if(music_songs[row]==song){
        out.push_back({labels,24,696,264,16,0,float(row*16),264,16});out.back().layer=layer;out.back().order=label_order;return;
    }
    // Original unknown/sentinel rows fall outside the 63-row picture. Keep
    // its blank name rather than selecting an unrelated song's label. When
    // music is stopped, the owner may still supply the loudest active effect.
}
}
