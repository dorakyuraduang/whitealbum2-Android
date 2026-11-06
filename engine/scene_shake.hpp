#pragma once
#include "scene_geometry.hpp"
#include "script_runtime.hpp"

namespace wa2::script {
// Frame-counted common scene effects. Resource, RNG and rendering ownership
// remain with the surrounding engine; no guest address or CPU state is used.
class SceneShake {
    std::function<std::uint32_t()> random_;
    std::int32_t mode_{},amplitude_{},duration_{},direction_{},speed_{};
    std::uint32_t elapsed_{};
    bool active_{},random_half_{};
    image::SceneTransform world_,dialogue_;
    static void validate(std::int32_t mode,std::int32_t amplitude,std::int32_t duration,std::int32_t direction,std::int32_t speed);
    std::uint32_t random();
public:
    explicit SceneShake(std::function<std::uint32_t()> random);
    std::optional<CommandResult> dispatch(Command&);
    void update(const std::function<std::int32_t(std::int32_t)>& duration={});
    void stop()noexcept;
    bool active()const noexcept{return active_;}
    const image::SceneTransform& world()const noexcept{return world_;}
    const image::SceneTransform& dialogue()const noexcept{return dialogue_;}
    std::vector<std::uint8_t> checkpoint()const;
    void restore(std::span<const std::uint8_t> bytes);
};
}
