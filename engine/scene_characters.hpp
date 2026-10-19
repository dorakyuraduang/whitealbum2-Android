#pragma once
#include "script_runtime.hpp"
#include "image_store.hpp"
#include "scene_sprite.hpp"

namespace wa2::script {
// Eight typed character slots replace 0x4160e0/0x402610 and their CPU state.
class SceneCharacters {
public:
    using Loader=std::function<std::shared_ptr<image::ImageRequest>(std::string_view)>;
    explicit SceneCharacters(Loader loader);
    std::optional<CommandResult> dispatch(Command& command);
    void update(const std::function<std::int32_t(std::int32_t)>& duration={});
    std::span<const image::Sprite> sprites();
    bool busy(std::int32_t id)const;
    void clear();
    void retire_background();
    void clear_retired();
    std::size_t count()const noexcept;
private:
    struct Pose {
        std::shared_ptr<const image::Raster> image;
        std::int32_t number{},layer{},alpha{255},brightness{128};
        float position{};
        bool operator==(const Pose&)const=default;
    };
    struct Slot {
        std::int32_t id{-1},effect{},duration{},elapsed{};
        float progress{};
        Pose current,previous;
        std::shared_ptr<image::ImageRequest> request;
        bool changing{},removing{},retiring{};
    };
    Loader loader_;
    std::array<Slot,8> slots_;
    std::vector<image::Sprite> sprites_;
    Slot* find(std::int32_t id);
    const Slot* find(std::int32_t id)const;
    void set(std::int32_t id,std::int32_t number,const Value& position,std::int32_t layer,
             std::int32_t effect,std::int32_t duration,std::int32_t alpha,std::int32_t brightness);
    void remove(std::int32_t id,std::int32_t effect,std::int32_t duration);
};
}
