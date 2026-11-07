#include "scene_voices.hpp"
#include <algorithm>
#include <limits>

namespace wa2::script {
SceneVoices::SceneVoices(Factory factory,std::function<std::int32_t()> chapter,std::function<std::int32_t()> speaker,
                         std::function<std::int32_t()> offset,std::function<bool()> skip,std::function<bool()> advance,Events events)
    :factory_(std::move(factory)),chapter_(std::move(chapter)),speaker_(std::move(speaker)),offset_(std::move(offset)),skip_(std::move(skip)),advance_(std::move(advance)),events_(std::move(events)){
    if(!factory_ || !chapter_ || !speaker_)throw Error("missing native voice environment");
}
SceneVoices::~SceneVoices(){for(auto& item:slots_)stop(item);}
SceneVoices::Slot& SceneVoices::checked(std::int32_t index){if(index<0 || std::size_t(index)>=slots_.size())throw Error("native voice channel out of range");return slots_[std::size_t(index)];}
void SceneVoices::stop(Slot& item){if(item.voice){if(events_.stopped)events_.stopped(std::uint32_t(&item-slots_.data()));item.voice->stop();}item={};}
std::size_t SceneVoices::active()const noexcept{std::size_t count=0;for(const auto& item:slots_)count+=bool(item.voice);return count;}
bool SceneVoices::loading()const{for(const auto& slot:slots_)if(slot.voice && !slot.voice->ready())return true;return false;}
void SceneVoices::apply(Slot& item){if(item.voice)item.voice->gain(std::uint8_t(std::clamp(std::int64_t(item.volume)*master_*speech_/65536,std::int64_t(0),std::int64_t(255))));}
void SceneVoices::settings(std::int32_t master,std::int32_t speech,bool mute_adult){
    master_=std::clamp(master,0,256);speech_=std::clamp(speech,0,256);mute_adult_=mute_adult;for(auto& item:slots_)apply(item);
}
void SceneVoices::enabled(bool value){
    if(enabled_==value)return;enabled_=value;
    // Cached 0x40ebc0 -> 0x4061c0 lowers existing channels to zero via
    // VXV(0,0). Re-enabling permits new lines, without restarting old speech.
    if(!value)for(auto& item:slots_)if(item.voice && !item.stopping){item.from=item.volume;item.target=0;item.duration=item.elapsed=0;item.fading=true;}
}
bool SceneVoices::permitted(std::int32_t speaker)const noexcept{
    // Cached 0x406380 normalizes supporting roles into the two original
    // voice groups. 0x406410 checks these ten switches before replacing a
    // channel. An unmapped speaker is permitted, not silently assigned to a
    // different character's switch.
    switch(speaker){
    case 12:case 13:case 15:case 17:case 18:case 20:case 21:case 27:case 28:case 30:case 31:case 32:case 36:case 37:case 99:speaker=99;break;
    case 14:case 16:case 19:case 22:case 23:case 24:case 25:case 26:case 29:case 33:case 34:case 35:case 98:speaker=98;break;
    }
    constexpr std::array<std::int32_t,10> groups{0,2,1,3,4,5,10,11,99,98};
    for(unsigned n=0;n<groups.size();++n)if(groups[n]==speaker)return characters_[n];return true;
}
void SceneVoices::preview(const SpokenLine& line){play(0,line.speaker,line.chapter,line.line,255,0,false);}
void SceneVoices::play(std::int32_t channel,std::int32_t speaker,std::int32_t chapter,std::int32_t line,std::int32_t volume,std::int32_t loop,bool record){
    auto& item=checked(channel);if(speaker==-1)speaker=speaker_();if(chapter==-1)chapter=chapter_();
    const auto adjusted=std::int64_t(line)+(offset_?offset_():0);
    if(chapter<0 || chapter>9999 || speaker<0 || speaker>99 || adjusted<0 || adjusted>9999)throw Error("native voice identity out of range");
    const SpokenLine line_info{speaker,chapter,std::int32_t(adjusted),loop!=0};if(record && events_.issued)events_.issued(std::uint32_t(channel),line_info,std::clamp(volume,0,255));
    // 0x461400 passes (mode == 0) as the repeat count. Both -1 and 1
    // therefore select continuous playback; scripts use -1 for ambient voice.
    // The original skips ordinary speech while fast-forward is held. A looped
    // /forced line remains audible, with the original four bounded channels.
    if(!enabled_ || !permitted(speaker) || (skip_ && skip_() && loop==0))return;
    if(mute_adult_ && adult_mode_ && speaker==0)return;
    stop(item);auto voice=factory_(line_info);if(!voice)throw Error("native voice loader returned no voice");
    item.volume=item.from=item.target=item.remembered=std::clamp(volume,0,255);item.line=line_info;item.voice=std::move(voice);apply(item);++played_;
    if(events_.started)events_.started(std::uint32_t(channel),line_info,*item.voice);
}
void SceneVoices::update(const std::function<std::int32_t(std::int32_t)>& adjust){
    for(auto& item:slots_)if(item.voice){
        if(const auto failure=item.voice->failure();!failure.empty())throw Error("speech playback failed: "+failure);
        if(item.voice->finished()){stop(item);continue;}
        if(!item.fading || !item.voice->ready())continue;
        const auto duration=adjust?adjust(item.duration):item.duration==-1?15:item.duration==-2?30:item.duration;
        if(duration<=0 || item.elapsed>=duration){item.volume=item.target;item.fading=false;if(item.stopping && !item.target){stop(item);continue;}}
        else{item.volume=std::int32_t((std::int64_t(duration-item.elapsed)*item.from+std::int64_t(item.elapsed)*item.target)/duration);++item.elapsed;}
        apply(item);
    }
}
void SceneVoices::stop(std::int32_t channel,std::int32_t fade){
    auto& item=checked(channel);if(!item.voice)return;
    if(!fade)stop(item);else{item.from=item.volume;item.target=0;item.duration=fade;item.elapsed=0;item.fading=item.stopping=true;}
}
std::optional<CommandResult> SceneVoices::dispatch(Command& command){
    const auto count=[&](std::size_t n){if(command.arguments.size()!=n)throw Error("invalid native voice parameters");};
    const auto v=[&](std::size_t n){return command.arguments[n].integer();};
    switch(command.index){
    // Existing 0x454db0/0x454de0 reorder the public VV/VX arguments before
    // calling 0x40eda0; channel and line are not in their first argument slots.
    case 138:count(5);play(v(3),v(0),-1,v(4),v(1),v(2));return CommandResult{true,false,Value{}};
    case 139:count(6);play(v(5),v(0),v(1),v(4),v(3),v(2));return CommandResult{true,false,Value{}};
    case 140:{count(2);const auto& item=checked(v(0));const bool interrupted=v(1)!=0 && ((skip_ && skip_()) || (advance_ && advance_()));
        return CommandResult{!item.voice || interrupted,true,Value{}};}
    // VS(fade, channel), matching the actual 0x454e40 stack order.
    case 141:count(2);stop(v(1),v(0));return CommandResult{true,false,Value{}};
    // Cached 0x456630 reorders VXV(volume, duration, channel) for 0x406160.
    // A VS fade locks the channel against further volume changes. VXV(0/1)
    // lowers its gain without stopping playback or losing the remembered gain.
    case 235:{count(3);auto& item=checked(v(2));if(!item.stopping){item.from=item.volume;item.target=std::clamp(v(0),0,255);
            if(v(0)>=2)item.remembered=item.target;item.duration=v(1);item.elapsed=0;item.fading=true;}
        return CommandResult{true,false,Value{}};}
    default:return std::nullopt;
    }
}
}
