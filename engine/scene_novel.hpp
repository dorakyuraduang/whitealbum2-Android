#pragma once
#include "script_runtime.hpp"

namespace wa2::script {
// Full-screen novel text uses the same dialogue engine and source font atlas.
// NB/NBR fade the black backing and hide/show its text independently of ADV's
// ordinary message-window opening animation.
class SceneNovel {
    std::int32_t duration_{};
    std::uint32_t elapsed_{};
    std::uint8_t alpha_{},opacity_{};
    bool enabled_{},hidden_{},text_visible_{true},active_{};
public:
    std::optional<CommandResult> dispatch(Command&);
    void update(const std::function<std::int32_t(std::int32_t)>& duration={});
    void alpha(std::int32_t value);
    bool enabled()const noexcept{return enabled_;}
    bool text_visible()const noexcept{return text_visible_;}
    bool busy()const noexcept{return active_ && enabled_;}
    std::uint8_t opacity()const noexcept{return enabled_?opacity_:0;}
    std::vector<std::uint8_t> checkpoint()const;
    void restore(std::span<const std::uint8_t>);
};
}
