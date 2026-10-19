#pragma once
#include "script_runtime.hpp"

namespace wa2::script {
struct SceneModes {
    std::int32_t time{},background_chroma{},character_chroma{},log_output{};
    std::string background_effect,character_effect;
    std::int32_t voice_chapter{-1},voice_offset{};
    bool draw_requested{};
};
// Ordinary gameplay state shared by scene scripts, audio and rendering. The
// platform supplies one monotonic millisecond clock and its input policy.
class SceneControls {
public:
    using Milliseconds=std::function<std::uint32_t()>;
    explicit SceneControls(Milliseconds clock,std::function<bool()> interrupt_timer={},std::function<bool()> interrupt_wait={},std::function<bool()> skipping={});
    std::optional<CommandResult> dispatch(Command& command);
    const SceneModes& modes() const noexcept {return modes_;}
    std::uint32_t timer_origin() const noexcept {return timer_origin_;}
    bool take_draw_request() noexcept {const auto value=modes_.draw_requested;modes_.draw_requested=false;return value;}
private:
    Milliseconds clock_;
    std::function<bool()> interrupt_timer_;
    std::function<bool()> interrupt_wait_;
    std::function<bool()> skipping_;
    SceneModes modes_;
    std::uint32_t timer_origin_{};
    std::uint32_t wait_counter_{},wait_value_{};
};
}
