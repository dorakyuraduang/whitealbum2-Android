#pragma once
#include "scene_background.hpp"
#include "scene_sprite.hpp"
#include "movie_reader.hpp"
#include <span>
#include <vector>

namespace wa2::game {
class Scene;
class TitleScene;
class Application;
// An owned presentation snapshot, independent of the device renderer and the
// lifetime of a scene. Images and YUV owners are shared, never pixel-copied.
// A host may retain a completed frame through a scene/audio ownership switch.
class RenderFrame {
    script::BackgroundFrame background_;
    std::vector<image::Sprite> environment_,overlays_;
    image::SceneTransform dialogue_;
    std::optional<movie::VideoFrame> movie_;
    bool movie_active_{};
public:
    void clear();
    void compose(const script::BackgroundFrame&,std::span<const image::Sprite> environment,
                 std::span<const std::span<const image::Sprite>> overlays,
                 const image::SceneTransform& dialogue={},bool movie_active=false,
                 std::optional<movie::VideoFrame> movie={});
    void capture(Scene&);
    void capture(const TitleScene&);
    // Full modal page views are still under construction. Until supplied they
    // fail explicitly, rather than passing an unpainted menu off as supported.
    void capture(Application&);
    const script::BackgroundFrame& background()const noexcept{return background_;}
    std::span<const image::Sprite> environment()const noexcept{return environment_;}
    std::span<const image::Sprite> overlays()const noexcept{return overlays_;}
    const image::SceneTransform& dialogue_transform()const noexcept{return dialogue_;}
    bool movie_active()const noexcept{return movie_active_;}
    const std::optional<movie::VideoFrame>& movie()const noexcept{return movie_;}
};
}
