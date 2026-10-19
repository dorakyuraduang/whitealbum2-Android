#pragma once
#include "script_runtime.hpp"
#include "stream_voice.hpp"

namespace wa2::script {
struct SpokenLine {std::int32_t speaker{},chapter{},line{};bool loop{};};
class SceneVoices {
public:
    using Factory=std::function<std::unique_ptr<audio::Voice>(const SpokenLine&)>;
    explicit SceneVoices(Factory factory,std::function<std::int32_t()> chapter,std::function<std::int32_t()> speaker,
                         std::function<std::int32_t()> offset={},std::function<bool()> skip={},std::function<bool()> advance={});
    ~SceneVoices();
    std::optional<CommandResult> dispatch(Command& command);
    void update(const std::function<std::int32_t(std::int32_t)>& duration={});
    std::size_t active()const noexcept;
    std::uint64_t played()const noexcept{return played_;}
private:
    struct Slot {std::unique_ptr<audio::Voice> voice;std::int32_t volume{},from{},duration{},elapsed{};bool fading{};};
    std::array<Slot,2> slots_;
    Factory factory_;
    std::function<std::int32_t()> chapter_,speaker_,offset_;
    std::function<bool()> skip_,advance_;
    std::uint64_t played_{};
    Slot& checked(std::int32_t index);
    void stop(Slot& item);
    void play(std::int32_t slot,std::int32_t speaker,std::int32_t chapter,std::int32_t line,std::int32_t volume,std::int32_t loop);
};
}
