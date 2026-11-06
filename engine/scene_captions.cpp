#include "scene_captions.hpp"
#include "script_runtime.hpp"
#include <limits>

namespace wa2::script {
unsigned SceneCaptions::index(subtitles::Kind kind,std::uint32_t slot){
    if((kind==subtitles::Kind::effect && slot>=16) || (kind==subtitles::Kind::voice && slot>=4))throw Error("Native caption audio slot out of range");
    if(kind!=subtitles::Kind::effect && kind!=subtitles::Kind::voice)throw Error("Invalid native caption audio kind");
    return (kind==subtitles::Kind::voice?16:0)+slot;
}
void SceneCaptions::clock(std::uint64_t now){
    if(!clock_started_){last_wall_=now;clock_started_=true;return;}
    if(now<last_wall_)throw Error("Native caption clock moved backwards");
    const auto delta=now-last_wall_;if(!paused_){if(delta>std::numeric_limits<std::uint64_t>::max()-logical_ms_)throw Error("Native caption clock overflow");logical_ms_+=delta;}last_wall_=now;
}
bool SceneCaptions::queue(std::uint32_t key,subtitles::Kind kind,std::uint32_t slot,const audio::Voice& voice,bool replace){
    const auto at=index(kind,slot);
    if((key<65536)!=(kind==subtitles::Kind::effect) || !subtitles::find_track(key))return false;
    if(sequence_==std::numeric_limits<std::uint64_t>::max())throw Error("Native caption sequence exhausted");
    pending_[at]={&voice,key,++sequence_,replace};return true;
}
void SceneCaptions::stop(subtitles::Kind kind,std::uint32_t slot)noexcept{
    const auto at=(kind==subtitles::Kind::voice?16u:0u)+slot;
    if((kind==subtitles::Kind::effect && slot<16) || (kind==subtitles::Kind::voice && slot<4)){pending_[at]={};playback_.stop(kind,slot);}
}
void SceneCaptions::reset()noexcept{pending_={};playback_.reset();baked_=nullptr;sprites_.clear();}
void SceneCaptions::update(std::uint64_t wall_ms){
    clock(wall_ms);(void)playback_.current(logical_ms_);if(paused_)return;
    // Arrival order is independent of channel number. A caption rejected while
    // another track is active is consumed once, matching Playback's policy.
    for(unsigned visited=0;visited<pending_.size();++visited){unsigned selected=unsigned(pending_.size());std::uint64_t sequence=std::numeric_limits<std::uint64_t>::max();
        for(unsigned n=0;n<pending_.size();++n){auto& candidate=pending_[n];if(!candidate.voice)continue;
            if(const auto failure=candidate.voice->failure();!failure.empty())throw Error("Native caption audio failed: "+failure);
            if(candidate.voice->finished()){candidate={};continue;}
            if(candidate.voice->ready() && candidate.sequence<sequence){selected=n;sequence=candidate.sequence;}}
        if(selected==pending_.size())break;const auto pending=pending_[selected];pending_[selected]={};
        playback_.play(pending.key,selected<16?subtitles::Kind::effect:subtitles::Kind::voice,selected<16?selected:selected-16,logical_ms_,pending.replace);
    }
}
std::span<const image::Sprite> SceneCaptions::sprites(){
    const auto* cue=current();if(cue==baked_)return sprites_;baked_=cue;sprites_.clear();if(!cue)return sprites_;
    const auto spec=subtitles::font_spec();const auto glyphs=subtitles::characters().size();
    if(!font_){auto decoded=subtitles::decode_font(subtitles::font_coverage(),spec,glyphs);font_=std::make_shared<const image::Raster>(image::Raster{decoded.width,decoded.height,std::move(decoded.rgba)});}
    const auto quads=subtitles::layout(*cue,spec,glyphs);sprites_.reserve(quads.size());
    for(const auto& q:quads){sprites_.push_back({font_,q.left,q.top,q.right-q.left,q.bottom-q.top,q.u0*font_->width,q.v0*font_->height,(q.u1-q.u0)*font_->width,(q.v1-q.v0)*font_->height});
        sprites_.back().layer=0x2000;sprites_.back().order=int(sprites_.size()-1);}
    ++layout_builds_;return sprites_;
}
}
