#pragma once
#include "script_runtime.hpp"

namespace wa2::script {
struct CameraPose {float x{},y{},scale_x{1},scale_y{1},angle{},pivot_x{640},pivot_y{360};};
struct CameraRetain {bool x{},y{},scale_x{},scale_y{},angle{};};
class SceneCamera {
    struct Tween {
        std::array<float,2> from{},target{},current{};
        std::int32_t duration{},easing{};
        std::uint32_t elapsed{};
        bool active{};
    };
    struct Bank {CameraPose pose;std::uint32_t width{1280},height{720};Tween pan,zoom,rotation;};
    std::array<Bank,2> banks_;
    std::int32_t paused_{},pause_ticks_{};
    std::uint32_t pause_elapsed_{};
    static float ease(std::int32_t mode,float progress);
    static void start(Tween&,std::array<float,2> current,std::array<float,2> target,std::int32_t ticks,std::int32_t mode);
public:
    void bind(unsigned bank,std::uint32_t width,std::uint32_t height,CameraPose pose);
    void replace(unsigned bank,std::uint32_t width,std::uint32_t height,CameraPose pose,CameraRetain retain);
    const CameraPose& pose(unsigned bank)const;
    bool busy()const noexcept;
    void update(const std::function<std::int32_t(std::int32_t)>& duration={});
    std::optional<CommandResult> dispatch(Command&,unsigned bank);
    std::vector<std::uint8_t> checkpoint()const;
    void restore(std::span<const std::uint8_t> bytes);
};
}
