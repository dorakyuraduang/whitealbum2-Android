#pragma once
#include "script_runtime.hpp"
#include "stream_voice.hpp"

namespace wa2::script{
struct EffectSlot{
    std::unique_ptr<audio::Voice> voice;
    std::int32_t sound{-1},volume{},from{},target{},duration{},elapsed{};
    bool loop{},stop_after_fade{},prepared{};
};
class SceneEffects{
public:
    using Factory=std::function<std::unique_ptr<audio::Voice>(std::int32_t,bool)>;
    using PreparedFactory=std::function<std::unique_ptr<audio::Voice>(std::int32_t,bool,bool)>;
    explicit SceneEffects(Factory,std::function<bool()> skip={},PreparedFactory prepared={});
    ~SceneEffects();
    void settings(std::int32_t master,std::int32_t effects);
    void play(std::int32_t channel,std::int32_t sound,std::int32_t fade,std::int32_t loop,std::int32_t volume,std::int32_t mode=0);
    void once(std::int32_t sound,std::int32_t volume);
    void stop(std::int32_t channel,std::int32_t fade=0);
    void stop_all();
    void volume(std::int32_t channel,std::int32_t value,std::int32_t fade);
    void update(const std::function<std::int32_t(std::int32_t)>& duration={});
    std::optional<CommandResult> dispatch(Command&);
    const EffectSlot& slot(std::int32_t channel)const;
private:
    Factory factory_;
    PreparedFactory prepared_factory_;
    std::function<bool()> skip_;
    std::array<EffectSlot,16> slots_;
    struct OneShot{std::unique_ptr<audio::Voice> voice;std::int32_t volume{};};
    std::array<OneShot,8> ones_;
    std::int32_t master_{256},effects_{256};
    std::uint8_t scaled(std::int32_t volume)const;
    EffectSlot& checked(std::int32_t channel);
};
}
