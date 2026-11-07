#pragma once
#include "script_runtime.hpp"

namespace wa2::script {
struct SceneModes {
    std::int32_t time{},background_chroma{},character_chroma{},log_output{};
    std::string background_effect,character_effect;
    std::int32_t voice_chapter{-1},voice_offset{};
    std::int32_t adult_mode{};
    std::int32_t demo_mode{},demo_ticks{},auto_ticks{};
    bool auto_mode{},latched_skip{};
    bool draw_requested{};
};
// Ordinary gameplay state shared by scene scripts, audio and rendering. The
// platform supplies one monotonic millisecond clock and its input policy.
class SceneControls {
public:
    using Milliseconds=std::function<std::uint32_t()>;
    explicit SceneControls(Milliseconds clock,std::function<bool()> interrupt_timer={},std::function<bool()> interrupt_wait={},std::function<bool()> skipping={},std::function<std::int32_t()> readable={});
    std::optional<CommandResult> dispatch(Command& command);
    const SceneModes& modes() const noexcept {return modes_;}
    std::uint32_t timer_origin() const noexcept {return timer_origin_;}
    bool take_draw_request() noexcept {const auto value=modes_.draw_requested;modes_.draw_requested=false;return value;}
    bool automatic_advance();
    void reset_message_timer()noexcept{message_elapsed_=0;}
    void auto_mode(bool value,std::int32_t ticks){modes_.auto_mode=value;modes_.auto_ticks=ticks;message_elapsed_=0;}
    void auto_delay(std::int32_t ticks)noexcept{modes_.auto_ticks=ticks;}
    void latched_skip(bool value){modes_.latched_skip=value;}
    std::vector<std::uint8_t> checkpoint()const;
    void restore(std::span<const std::uint8_t> bytes);
private:
    Milliseconds clock_;
    std::function<bool()> interrupt_timer_;
    std::function<bool()> interrupt_wait_;
    std::function<bool()> skipping_;
    std::function<std::int32_t()> readable_;
    SceneModes modes_;
    std::uint32_t timer_origin_{};
    std::uint32_t wait_counter_{},wait_value_{};
    std::uint32_t message_elapsed_{};
};
}
