#pragma once
#include "script_runtime.hpp"
#include "stream_voice.hpp"

namespace wa2::script {
struct SpokenLine {std::int32_t speaker{},chapter{},line{};bool loop{};};
class SceneVoices {
public:
    using Factory=std::function<std::unique_ptr<audio::Voice>(const SpokenLine&)>;
    struct Events {std::function<void(std::uint32_t,const SpokenLine&,const audio::Voice&)> started;std::function<void(std::uint32_t)> stopped;
        std::function<void(std::uint32_t,const SpokenLine&,std::int32_t)> issued;};
    explicit SceneVoices(Factory factory,std::function<std::int32_t()> chapter,std::function<std::int32_t()> speaker,
                         std::function<std::int32_t()> offset={},std::function<bool()> skip={},std::function<bool()> advance={},Events events={});
    ~SceneVoices();
    std::optional<CommandResult> dispatch(Command& command);
    void update(const std::function<std::int32_t(std::int32_t)>& duration={});
    void stop(std::int32_t channel,std::int32_t fade=0);
    // Source settings use channel zero without adding a story/backlog entry.
    void preview(const SpokenLine&);
    std::size_t active()const noexcept;
    bool loading()const;
    std::uint64_t played()const noexcept{return played_;}
    void settings(std::int32_t master,std::int32_t speech,bool mute_adult=false);
    void enabled(bool value);
    void character_switches(std::array<bool,10> value)noexcept{characters_=value;}
    void adult_mode(bool value)noexcept{adult_mode_=value;}
    std::vector<std::uint8_t> checkpoint()const;
    void restore(std::span<const std::uint8_t>);
private:
    struct Slot {std::unique_ptr<audio::Voice> voice;SpokenLine line;std::int32_t volume{},from{},target{},remembered{},duration{},elapsed{};bool fading{},stopping{};};
    std::array<Slot,4> slots_;
    Factory factory_;
    std::function<std::int32_t()> chapter_,speaker_,offset_;
    std::function<bool()> skip_,advance_;
    Events events_;
    std::uint64_t played_{};
    std::int32_t master_{256},speech_{256};
    bool mute_adult_{},adult_mode_{};
    bool enabled_{true};
    std::array<bool,10> characters_{true,true,true,true,true,true,true,true,true,true};
    bool permitted(std::int32_t speaker)const noexcept;
    void apply(Slot&);
    Slot& checked(std::int32_t index);
    void stop(Slot& item);
    void play(std::int32_t slot,std::int32_t speaker,std::int32_t chapter,std::int32_t line,std::int32_t volume,std::int32_t loop,bool record=true);
};
}
