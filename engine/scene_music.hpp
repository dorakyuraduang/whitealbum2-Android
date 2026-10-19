#pragma once
#include "script_runtime.hpp"
#include "stream_voice.hpp"

namespace wa2::script {
enum class MusicPhase { idle=0,playing=1,fade_in=3,fade_out=4,volume=5 };
struct MusicSlot {
    std::int32_t song{-1},elapsed{},duration{1},volume{},from{},target{};
    MusicPhase phase{MusicPhase::idle};
    std::unique_ptr<audio::Voice> voice;
};
// Music updates use the game's logic ticks. A platform may render more often
// without shortening script fades; the game host supplies any skip policy.
class SceneMusic {
public:
    using Factory=std::function<std::unique_ptr<audio::Voice>(std::int32_t,bool)>;
    using Duration=std::function<std::int32_t(std::int32_t)>;
    explicit SceneMusic(Factory factory,std::function<void(std::int32_t)> seen={});
    ~SceneMusic();
    std::optional<CommandResult> dispatch(Command& command);
    void update(const Duration& duration={});
    void settings(std::int32_t master,std::int32_t music);
    void play(std::int32_t song,std::int32_t fade,std::int32_t mode,std::int32_t volume,bool force=false);
    void stop(std::int32_t fade);
    void volume(std::int32_t value,std::int32_t fade);
    std::int32_t song()const noexcept{return song_;}
    const MusicSlot& current()const noexcept{return slots_[current_];}
    const std::array<MusicSlot,2>& slots()const noexcept{return slots_;}
private:
    Factory factory_;
    std::function<void(std::int32_t)> seen_;
    std::array<MusicSlot,2> slots_;
    std::size_t current_{};
    std::int32_t song_{-1},master_{256},music_{256};
    void fade_out(MusicSlot& slot,std::int32_t frames);
    void apply(MusicSlot& slot);
};
}
