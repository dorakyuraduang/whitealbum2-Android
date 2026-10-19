#include "scene_voices.hpp"
#include <algorithm>
#include <limits>

namespace wa2::script {
SceneVoices::SceneVoices(Factory factory,std::function<std::int32_t()> chapter,std::function<std::int32_t()> speaker,
                         std::function<std::int32_t()> offset,std::function<bool()> skip,std::function<bool()> advance)
    :factory_(std::move(factory)),chapter_(std::move(chapter)),speaker_(std::move(speaker)),offset_(std::move(offset)),skip_(std::move(skip)),advance_(std::move(advance)){
    if(!factory_ || !chapter_ || !speaker_)throw Error("missing native voice environment");
}
SceneVoices::~SceneVoices(){for(auto& item:slots_)stop(item);}
SceneVoices::Slot& SceneVoices::checked(std::int32_t index){if(index<0 || std::size_t(index)>=slots_.size())throw Error("native voice channel out of range");return slots_[std::size_t(index)];}
void SceneVoices::stop(Slot& item){if(item.voice)item.voice->stop();item={};}
std::size_t SceneVoices::active()const noexcept{std::size_t count=0;for(const auto& item:slots_)count+=bool(item.voice);return count;}
void SceneVoices::play(std::int32_t channel,std::int32_t speaker,std::int32_t chapter,std::int32_t line,std::int32_t volume,std::int32_t loop){
    auto& item=checked(channel);if(speaker==-1)speaker=speaker_();if(chapter==-1)chapter=chapter_();
    const auto adjusted=std::int64_t(line)+(offset_?offset_():0);
    if(chapter<0 || chapter>9999 || speaker<0 || speaker>99 || adjusted<0 || adjusted>9999)throw Error("native voice identity out of range");
    // 0x461400 passes (mode == 0) as the repeat count. Both -1 and 1
    // therefore select continuous playback; scripts use -1 for ambient voice.
    // The original skips ordinary speech while fast-forward is held. A looped
    // /forced line remains audible, with the same bounded two-slot lifetime.
    if(skip_ && skip_() && loop==0){stop(item);return;}
    stop(item);auto voice=factory_({speaker,chapter,std::int32_t(adjusted),loop!=0});if(!voice)throw Error("native voice loader returned no voice");
    item.volume=std::clamp(volume,0,255);voice->gain(std::uint8_t(item.volume));item.voice=std::move(voice);++played_;
}
void SceneVoices::update(const std::function<std::int32_t(std::int32_t)>& adjust){
    for(auto& item:slots_)if(item.voice){
        if(const auto failure=item.voice->failure();!failure.empty())throw Error("speech playback failed: "+failure);
        if(item.voice->finished()){stop(item);continue;}
        if(!item.fading || !item.voice->ready())continue;
        const auto duration=adjust?adjust(item.duration):item.duration;
        if(duration<=0 || ++item.elapsed>=duration){stop(item);continue;}
        item.volume=std::int32_t(std::int64_t(duration-item.elapsed)*item.from/duration);item.voice->gain(std::uint8_t(item.volume));
    }
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
    case 141:{count(2);auto& item=checked(v(1));if(item.voice){if(v(0)<=0)stop(item);else{item.from=item.volume;item.duration=v(0);item.elapsed=0;item.fading=true;}}
        return CommandResult{true,false,Value{}};}
    default:return std::nullopt;
    }
}
}
