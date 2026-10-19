#pragma once
#include "script_runtime.hpp"
#include "image_store.hpp"

namespace wa2::script {
struct BackgroundLayer {
    std::shared_ptr<const image::Raster> image;
    float x{},y{},width{},height{};
    std::uint8_t opacity{255};
    std::array<std::int32_t,3> colour{128,128,128};
};
struct BackgroundFrame {
    std::optional<BackgroundLayer> previous,current;
    struct Mask {
        std::shared_ptr<const image::Raster> image;
        float bias{},gain{1};
        bool flip_x{},flip_y{},invert{};
    };
    std::optional<Mask> mask;
};
// Original B/BW image selection, placement, dissolves and PAK mask transitions.
class SceneBackground {
public:
    using Loader=std::function<std::shared_ptr<image::ImageRequest>(std::string_view)>;
    explicit SceneBackground(Loader loader,std::function<void(bool)> changing={},Loader masks={},Loader events={});
    std::optional<CommandResult> dispatch(Command& command,std::int32_t time_mode);
    void update(const std::function<std::int32_t(std::int32_t)>& duration={});
    BackgroundFrame frame()const;
    bool busy()const noexcept{return loading_ || active_;}
    bool picture()const noexcept{return picture_;}
private:
    Loader loader_;
    Loader mask_loader_;
    Loader event_loader_;
    std::function<void(bool)> changing_;
    std::shared_ptr<image::ImageRequest> request_;
    std::shared_ptr<image::ImageRequest> mask_request_;
    std::optional<BackgroundFrame::Mask> mask_;
    std::array<std::optional<BackgroundLayer>,2> layers_;
    std::size_t current_{};
    std::int32_t elapsed_{},duration_{1},effect_{1},mask_mode_{},x_{},y_{};
    float width_{1280},height_{720};
    bool loading_{},active_{},picture_{};
    void begin(Command& command,std::int32_t time_mode);
};
}
