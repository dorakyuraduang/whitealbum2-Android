#pragma once
#include "script_runtime.hpp"
#include "image_store.hpp"
#include "scene_sprite.hpp"
#include "sprite_animation.hpp"

namespace wa2::script {
struct BitmapSlot {
    std::shared_ptr<image::ImageRequest> request;
    std::shared_ptr<const image::Raster> image;
    std::shared_ptr<const image::Animation> animation;
    std::optional<image::AnimationPlayer> player;
    std::vector<std::shared_ptr<image::ImageRequest>> texture_requests;
    std::vector<std::shared_ptr<const image::Raster>> textures;
    float x{},y{},width{},height{},source_x{},source_y{},source_width{},source_height{},scale{1},angle{},pivot_x{},pivot_y{};
    std::int32_t layer{},blend{1},alpha{255},from{255},target{255},elapsed{},duration{};
    std::int32_t order_index{};
    std::array<std::int32_t,3> colour{128,128,128};
    std::uint32_t flip{};
    bool allocated{},visible{true},independent{};
    bool retiring{};
    bool loading()const noexcept{if(request)return true;for(const auto& item:texture_requests)if(item)return true;return false;}
    bool animation_validated{};
};
class SceneBitmaps {
public:
    using Loader=std::function<std::shared_ptr<image::ImageRequest>(std::string_view,std::string_view,std::int32_t)>;
    using AnimationLoader=std::function<std::shared_ptr<const image::Animation>(std::string_view,std::string_view)>;
    explicit SceneBitmaps(Loader loader,AnimationLoader animations={});
    std::optional<CommandResult> dispatch(Command& command);
    void update(const std::function<std::int32_t(std::int32_t)>& duration={});
    const BitmapSlot& slot(std::int32_t index)const;
    std::span<const image::Sprite> sprites(std::int32_t first_layer=0,std::int32_t last_layer=255,int independent=-1);
    void retire_background(){for(auto& item:slots_)if(item.allocated && !item.independent)item.retiring=true;}
    void clear_retired(){for(auto& item:slots_)if(item.retiring)item={};retired_.clear();}
    void clear();
    bool loading()const noexcept;
private:
    Loader loader_;
    AnimationLoader animation_loader_;
    std::array<BitmapSlot,32> slots_;
    std::vector<BitmapSlot> retired_;
    std::vector<image::Sprite> sprites_;
    BitmapSlot& checked(std::int32_t index);
};
}
