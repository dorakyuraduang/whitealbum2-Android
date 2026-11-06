#pragma once
#include "script_runtime.hpp"
#include "image_store.hpp"
#include "native_random.hpp"
#include "scene_sprite.hpp"
#include "sprite_animation.hpp"

namespace wa2::script {
struct WeatherParticle {
    bool active{};
    std::int16_t kind{},phase{};
    float angle{},radius{},rate{},rotation{},scale{};
    float growth{1},velocity{};
};
class SceneWeather {
public:
    using Loader=std::function<std::shared_ptr<image::ImageRequest>(std::string_view)>;
    using Random=std::function<std::uint32_t()>;
    using AnimationLoader=std::function<std::shared_ptr<const image::Animation>(std::string_view)>;
    SceneWeather(Loader loader,Random random,AnimationLoader animations={});
    std::optional<CommandResult> dispatch(Command& command);
    void update();
    void stop(bool immediately);
    std::span<const image::Sprite> sprites()const noexcept{return sprites_;}
    std::span<const WeatherParticle,400> particles()const noexcept{return particles_;}
    std::int32_t type()const noexcept{return type_;}
    std::int32_t scope()const noexcept{return scope_;}
    std::int32_t priority()const noexcept{return priority_;}
    std::size_t extent()const noexcept{return extent_;}
    bool active()const noexcept{return enabled_;}
    bool loading()const noexcept{if(request_)return true;for(const auto& request:animation_requests_)if(request)return true;return false;}
private:
    Loader loader_;
    Random random_;
    AnimationLoader animation_loader_;
    std::shared_ptr<const image::Animation> animation_;
    std::optional<image::AnimationPlayer> animation_player_;
    std::vector<std::shared_ptr<image::ImageRequest>> animation_requests_;
    std::vector<std::shared_ptr<const image::Raster>> animation_textures_;
    bool animation_validated_{};
    std::shared_ptr<image::ImageRequest> request_;
    std::shared_ptr<const image::Raster> image_;
    std::array<WeatherParticle,400> particles_{};
    std::vector<image::Sprite> sprites_;
    std::size_t extent_{};
    std::uint32_t frame_{};
    std::int32_t type_{},flags_{},priority_{},scope_{},density_{},target_x_{},target_y_{},amplitude_{},target_amplitude_{};
    float speed_x_{},speed_y_{};
    bool enabled_{},prefill_{},stopping_{};
    std::uint32_t random();
    void create(std::size_t index,bool filled,bool opposite=false);
    void begin(Command& command);
    bool stopped();
};
}
