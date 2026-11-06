#pragma once
#include "script_runtime.hpp"
#include "image_store.hpp"
#include "scene_sprite.hpp"
#include "image_reference.hpp"

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
    bool loading()const noexcept;
    void clear();
    void retire_background();
    void clear_retired();
    void invalidate_image_style(){if(style_generation_==std::uint64_t(-1))throw Error("Native character style generation exhausted");++style_generation_;}
    std::size_t count()const noexcept;
    std::vector<std::uint8_t> checkpoint(const resource::Catalog&)const;
    void restore(std::span<const std::uint8_t>,resource::Catalog&,const image::Resolve&);
private:
    struct Pose {
        std::shared_ptr<const image::Raster> image;
        std::int32_t number{},layer{},alpha{255},brightness{128};
        float position{};
        std::uint64_t style_generation{};
        bool operator==(const Pose&)const=default;
    };
    struct Slot {
        std::int32_t id{-1},effect{},duration{},elapsed{},order_index{};
        float progress{};
        Pose current,previous;
        std::shared_ptr<image::ImageRequest> request;
        bool changing{},removing{},retiring{};
    };
    Loader loader_;
    std::array<Slot,8> slots_;
    std::vector<Slot> retired_;
    std::uint64_t style_generation_{};
    std::vector<image::Sprite> sprites_;
    Slot* find(std::int32_t id);
    const Slot* find(std::int32_t id)const;
    void set(std::int32_t id,std::int32_t number,const Value& position,std::int32_t layer,
             std::int32_t effect,std::int32_t duration,std::int32_t alpha,std::int32_t brightness);
    void remove(std::int32_t id,std::int32_t effect,std::int32_t duration);
};
}
