#pragma once
#include "script_runtime.hpp"

namespace wa2::script {
// Native counterpart of 0x401de0/0x40d240: 128 means unchanged, smaller
// channels multiply the background and larger channels add specular colour.
class SceneColour {
    std::array<std::int32_t,3> current_{128,128,128},from_{128,128,128},target_{128,128,128};
    std::int32_t duration_{},second_duration_{},elapsed_{};
    bool active_{},returning_{};
public:
    std::optional<CommandResult> dispatch(Command& command);
    void update(const std::function<std::int32_t(std::int32_t)>& adjust={});
    bool busy()const noexcept{return active_;}
    std::array<std::int32_t,3> channels()const noexcept{return current_;}
};
}
