#pragma once
#include "script_runtime.hpp"
#include "image_store.hpp"
#include "scene_camera.hpp"
#include "scene_geometry.hpp"

namespace wa2::resource {class Catalog;}
namespace wa2::image {struct Reference;}

namespace wa2::script {
struct BackgroundLayer {
    std::shared_ptr<const image::Raster> image;
    float x{},y{},width{},height{};
    std::uint8_t opacity{255};
    std::array<std::int32_t,3> colour{128,128,128};
    float angle{},pivot_x{640},pivot_y{360};
    float source_x{},source_y{},source_width{},source_height{},scale_x{1},scale_y{1};
    bool operator==(const BackgroundLayer&)const=default;
};
struct BackgroundFrame {
    std::optional<BackgroundLayer> previous,current;
    struct Mask {
        std::shared_ptr<const image::Raster> image;
        float bias{},gain{1};
        bool flip_x{},flip_y{},invert{};
    };
    std::optional<Mask> mask;
    image::SceneTransform transform;
    struct Composite {image::SceneTransform transform;std::uint8_t opacity{255};};
    struct Transition {Composite previous,current;};
    std::optional<Transition> transition;
};
// Original B/BW image selection, placement, dissolves and PAK mask transitions.
class SceneBackground {
public:
    using Loader=std::function<std::shared_ptr<image::ImageRequest>(std::string_view)>;
    using Resolve=std::function<std::shared_ptr<const image::Raster>(const image::Reference&)>;
    explicit SceneBackground(Loader loader,std::function<void(bool)> changing={},Loader masks={},Loader events={},std::function<void(unsigned,std::int32_t)> seen={});
    std::optional<CommandResult> dispatch(Command& command,std::int32_t time_mode);
    void update(const std::function<std::int32_t(std::int32_t)>& duration={});
    BackgroundFrame frame()const;
    bool busy()const noexcept{return loading_ || active_;}
    bool loading()const noexcept{return loading_;}
    bool picture()const noexcept{return picture_;}
    const SceneCamera& camera()const noexcept{return camera_;}
    std::vector<std::uint8_t> checkpoint(const resource::Catalog&)const;
    // Reconstruct a settled view from typed native values, without a scene
    // change notification, command continuation or progress callback.
    void restore_still(std::shared_ptr<const image::Raster>,CameraPose,bool picture);
    // A restore worker resolves immutable pixels; no GPU/window operation is
    // performed here. Failed parsing/resolution leaves this controller intact.
    void restore(std::span<const std::uint8_t>,resource::Catalog&,const Resolve&);
private:
    Loader loader_;
    Loader mask_loader_;
    Loader event_loader_;
    std::function<void(bool)> changing_;
    std::function<void(unsigned,std::int32_t)> seen_;
    std::shared_ptr<image::ImageRequest> request_;
    std::shared_ptr<const image::Raster> ready_image_;
    std::shared_ptr<image::ImageRequest> mask_request_;
    std::optional<BackgroundFrame::Mask> mask_;
    std::array<std::optional<BackgroundLayer>,2> layers_;
    std::size_t current_{};
    SceneCamera camera_;
    std::int32_t elapsed_{},duration_{1},effective_duration_{1},effect_{1},mask_mode_{},x_{},y_{};
    float width_{1280},height_{720},angle_{};
    bool extended_{};
    bool loading_{},active_{},picture_{};
    void begin(Command& command,std::int32_t time_mode);
};
}
