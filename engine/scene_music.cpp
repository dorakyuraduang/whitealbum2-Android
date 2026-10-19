#include "scene_music.hpp"
#include <algorithm>

namespace wa2::script {
SceneMusic::SceneMusic(Factory factory,std::function<void(std::int32_t)> seen)
    :factory_(std::move(factory)),seen_(std::move(seen)){
    if(!factory_)throw Error("missing native music loader");
}
SceneMusic::~SceneMusic(){for(auto& slot:slots_)if(slot.voice)slot.voice->stop();}
void SceneMusic::apply(MusicSlot& slot){
    if(slot.voice)slot.voice->gain(std::uint8_t(std::clamp(std::int64_t(slot.volume)*master_*music_/65536,std::int64_t(0),std::int64_t(255))));
}
void SceneMusic::settings(std::int32_t master,std::int32_t music){
    master_=std::clamp(master,0,256);music_=std::clamp(music,0,256);for(auto& slot:slots_)apply(slot);
}
void SceneMusic::fade_out(MusicSlot& slot,std::int32_t frames){
    if(slot.song==-1)return;
    slot.phase=MusicPhase::fade_out;slot.elapsed=0;slot.duration=std::max(1,frames);slot.from=slot.volume;slot.target=0;
}
void SceneMusic::stop(std::int32_t frames){song_=-1;fade_out(slots_[current_],frames);}
void SceneMusic::volume(std::int32_t value,std::int32_t frames){
    auto& slot=slots_[current_];if(slot.song==-1)return;
    slot.phase=MusicPhase::volume;slot.elapsed=0;slot.duration=std::max(1,frames);slot.from=slot.volume;slot.target=std::clamp(value,0,255);
}
void SceneMusic::play(std::int32_t song,std::int32_t fade,std::int32_t mode,std::int32_t value,bool force){
    if(std::uint32_t(song)>=200){stop(fade);return;}
    if(seen_)seen_(song);
    if(!force && song==song_){volume(value,fade);return;}
    auto voice=factory_(song,mode==0);if(!voice)throw Error("native music loader returned no voice");
    const auto next=current_^1;auto& slot=slots_[next];if(slot.voice)slot.voice->stop();
    slot=MusicSlot{};slot.song=song;slot.phase=MusicPhase::fade_in;
    slot.duration=std::max(1,fade==-2?0:fade);slot.target=std::clamp(value,0,255);
    slot.volume=slot.from=(fade<1)?slot.target:0;slot.voice=std::move(voice);apply(slot);
    fade_out(slots_[current_],fade==-2?15:fade);current_=next;song_=song;
}
void SceneMusic::update(const Duration& adjust){
    for(auto& slot:slots_){
        if(!slot.voice || slot.phase==MusicPhase::idle)continue;
        if(const auto error=slot.voice->failure();!error.empty())throw Error("music playback failed: "+error);
        if(!slot.voice->ready())continue;
        const auto frames=adjust?adjust(slot.duration):slot.duration;
        if(slot.phase==MusicPhase::fade_out && slot.elapsed>=frames){
            slot.voice->stop();slot=MusicSlot{};continue;
        }
        if(slot.phase==MusicPhase::fade_in || slot.phase==MusicPhase::fade_out || slot.phase==MusicPhase::volume){
            if(frames<=0 || slot.elapsed>=frames){slot.volume=slot.target;slot.phase=MusicPhase::playing;}
            else{slot.volume=std::int32_t((std::int64_t(slot.from)*(frames-slot.elapsed)+std::int64_t(slot.target)*slot.elapsed)/frames);++slot.elapsed;}
            apply(slot);
        }
    }
}
std::optional<CommandResult> SceneMusic::dispatch(Command& command){
    const auto count=[&](std::size_t n){if(command.arguments.size()!=n)throw Error("invalid native music parameters");};
    const auto integer=[&](std::size_t n){return command.arguments[n].integer();};
    switch(command.index){
    case 158:count(4);play(integer(0),integer(1),integer(2),integer(3));return CommandResult{true,false,Value{}};
    case 159:count(1);stop(integer(0));return CommandResult{true,false,Value{}};
    case 160:count(0);return CommandResult{true,false,Value{}}; // The existing MP callback is empty.
    case 161:count(2);volume(integer(0),integer(1));return CommandResult{true,false,Value{}};
    case 162:count(0);return CommandResult{current().phase==MusicPhase::idle || current().phase==MusicPhase::playing,true,Value{}};
    case 163:count(0);return CommandResult{current().phase==MusicPhase::idle,true,Value{}};
    default:return std::nullopt;
    }
}
}
