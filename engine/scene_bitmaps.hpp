#pragma once
#include "script_runtime.hpp"
#include "image_store.hpp"
#include "scene_sprite.hpp"

namespace wa2::script {
struct BitmapSlot {
    std::shared_ptr<image::ImageRequest> request;
    std::shared_ptr<const image::Raster> image;
    float x{},y{},width{},height{},source_x{},source_y{},source_width{},source_height{},scale{1},angle{},pivot_x{},pivot_y{};
    std::int32_t layer{},blend{1},alpha{255},from{255},target{255},elapsed{},duration{};
    std::array<std::int32_t,3> colour{128,128,128};
    std::uint32_t flip{};
    bool allocated{},visible{true},independent{};
    bool retiring{};
};
class SceneBitmaps {
public:
    using Loader=std::function<std::shared_ptr<image::ImageRequest>(std::string_view,std::string_view,std::int32_t)>;
    explicit SceneBitmaps(Loader loader);
    std::optional<CommandResult> dispatch(Command& command);
    void update(const std::function<std::int32_t(std::int32_t)>& duration={});
    const BitmapSlot& slot(std::int32_t index)const;
    std::span<const image::Sprite> sprites(std::int32_t first_layer=0,std::int32_t last_layer=255,int independent=-1);
    void retire_background(){for(auto& item:slots_)if(item.allocated && !item.independent)item.retiring=true;}
    void clear_retired(){for(auto& item:slots_)if(item.retiring)item={};}
    void clear();
private:
    Loader loader_;
    std::array<BitmapSlot,32> slots_;
    std::vector<image::Sprite> sprites_;
    BitmapSlot& checked(std::int32_t index);
};
}
