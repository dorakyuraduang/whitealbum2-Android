#include "scene_effects.hpp"
#include <algorithm>

namespace wa2::script{
namespace{
void check_voice(const audio::Voice& voice){if(const auto error=voice.failure();!error.empty())throw Error("effect playback failed: "+error);}
}
SceneEffects::SceneEffects(Factory factory,std::function<bool()> skip,PreparedFactory prepared):factory_(std::move(factory)),prepared_factory_(std::move(prepared)),skip_(std::move(skip)){
    if(!factory_)throw Error("missing native sound-effect loader");
}
SceneEffects::~SceneEffects(){stop_all();}
std::uint8_t SceneEffects::scaled(std::int32_t volume)const{
    return std::uint8_t(std::clamp(std::int64_t(volume)*master_*effects_/65536,std::int64_t(0),std::int64_t(255)));
}
EffectSlot& SceneEffects::checked(std::int32_t channel){
    if(channel<0 || channel>=std::int32_t(slots_.size()))throw Error("invalid native sound-effect channel");
    return slots_[std::size_t(channel)];
}
const EffectSlot& SceneEffects::slot(std::int32_t channel)const{
    if(channel<0 || channel>=std::int32_t(slots_.size()))throw Error("invalid native sound-effect channel");
    return slots_[std::size_t(channel)];
}
void SceneEffects::settings(std::int32_t master,std::int32_t effects){
    master_=std::clamp(master,0,256);effects_=std::clamp(effects,0,256);
    for(auto& slot:slots_)if(slot.voice)slot.voice->gain(scaled(slot.volume));
    for(auto& value:ones_)if(value.voice)value.voice->gain(scaled(value.volume));
}
void SceneEffects::once(std::int32_t sound,std::int32_t volume){
    for(auto& value:ones_)if(!value.voice){
        auto voice=factory_(sound,false);if(!voice)throw Error("native effect loader returned no voice");
        value.volume=std::clamp(volume,0,255);voice->gain(scaled(value.volume));value.voice=std::move(voice);return;
    }
    // The original ordinary SE pool has eight entries and drops a ninth SE.
}
void SceneEffects::play(std::int32_t channel,std::int32_t sound,std::int32_t fade,std::int32_t loop,std::int32_t volume,std::int32_t mode){
    auto& value=checked(channel);
    if(mode<0 || mode>2)throw Unsupported("native sound-effect mode "+std::to_string(mode));
    if(mode==2){
        if(!value.voice || !value.prepared)throw Error("native sound-effect channel was not prepared");
        if(value.loop!=(loop!=0))throw Unsupported("native prepared effect loop change");
        value.prepared=false;value.duration=fade;value.elapsed=0;value.from=0;value.target=std::clamp(volume,0,255);value.volume=fade?0:value.target;
        value.voice->gain(scaled(value.volume));value.voice->pause(false);return;
    }
    if(mode==1){
        if(!prepared_factory_)throw Unsupported("native sound-effect factory has no prepared transport");
        auto voice=prepared_factory_(sound,loop!=0,true);if(!voice)throw Error("native prepared effect loader returned no voice");
        if(value.voice)value.voice->stop();value={};value.voice=std::move(voice);value.sound=sound;value.loop=loop!=0;value.prepared=true;return;
    }
    auto voice=factory_(sound,loop!=0);if(!voice)throw Error("native effect loader returned no voice");
    if(value.voice)value.voice->stop();value={};value.sound=sound;value.duration=fade;value.loop=loop!=0;
    value.target=std::clamp(volume,0,255);value.volume=fade?0:value.target;voice->gain(scaled(value.volume));value.voice=std::move(voice);
}
void SceneEffects::volume(std::int32_t channel,std::int32_t volume,std::int32_t fade){
    auto& value=checked(channel);if(!value.voice)return;
    value.from=value.volume;value.target=std::clamp(volume,0,255);value.duration=fade;value.elapsed=0;
}
void SceneEffects::stop(std::int32_t channel,std::int32_t fade){
    auto& value=checked(channel);if(!value.voice)return;
    if(fade>0){volume(channel,0,fade);value.stop_after_fade=true;return;}
    value.voice->stop();value={};
}
void SceneEffects::stop_all(){
    for(auto& value:slots_){if(value.voice)value.voice->stop();value={};}
    for(auto& value:ones_){if(value.voice)value.voice->stop();value={};}
}
void SceneEffects::update(const std::function<std::int32_t(std::int32_t)>& adjust){
    for(auto& value:ones_)if(value.voice){check_voice(*value.voice);if(value.voice->finished()){value.voice->stop();value={};}}
    for(auto& value:slots_)if(value.voice){
        check_voice(*value.voice);
        if(value.voice->finished()){value.voice->stop();value={};continue;}
        if(value.prepared)continue;
        if(!value.voice->ready())continue;
        const auto frames=adjust?adjust(value.duration):value.duration;
        if(frames<=0 || value.elapsed>=frames){
            value.volume=value.target;
            if(value.stop_after_fade){value.voice->stop();value={};return;}
        }else{
            value.volume=std::int32_t((std::int64_t(frames-value.elapsed)*value.from+std::int64_t(value.target)*value.elapsed)/frames);++value.elapsed;
        }
        value.voice->gain(scaled(value.volume));
    }
}
std::optional<CommandResult> SceneEffects::dispatch(Command& command){
    const auto count=[&](std::size_t n){if(command.arguments.size()!=n)throw Error("invalid native effect parameters");};
    const auto integer=[&](std::size_t n){return command.arguments[n].integer();};
    switch(command.index){
    case 164:count(2);once(integer(0),integer(1));break;
    case 165:count(6);play(integer(0),integer(1),integer(2),integer(3),integer(4),integer(5));break;
    case 166:count(2);stop(integer(0),integer(1));break;
    case 167:count(3);volume(integer(0),integer(1),integer(2));break;
    case 168:{
        count(2);if(!integer(1) && skip_ && skip_())stop(integer(0));
        const auto& value=slot(integer(0));return CommandResult{!value.voice,true,Value{}};
    }
    case 169:{
        count(1);const auto& value=slot(integer(0));
        // Preserve the actual 0x4557e0 -> 0x406880 callback predicate. It
        // remains pending when the current and target volumes are equal.
        return CommandResult{value.volume!=value.target,true,Value{}};
    }
    default:return {};
    }
    return CommandResult{true,false,Value{}};
}
}
