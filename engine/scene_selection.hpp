#pragma once
#include "scene_choices.hpp"
#include "image_store.hpp"
#include "text_layout.hpp"

namespace wa2::script {
struct SelectionSettings {
    std::int32_t skin{};
    bool replay{};
    text::Colour normal{255,255,255},highlight{255,247,153},disabled{160,160,160};
};
struct SelectionInput {bool pointer{},activate{},confirm{};float x{},y{};std::int32_t navigation{};};
enum class SelectionSound {focus,accept,reject};
struct SelectionRow {std::uint32_t ordinal{};float y{};bool enabled{},chosen{};};
// Shared original selection layout and focus/input state. Platform input must
// already be expressed in the game's logical 1280x720 coordinate space.
class SceneSelection {
public:
    using Loader=std::function<std::shared_ptr<image::ImageRequest>(std::string_view)>;
    using Text=std::function<text::PreparedText(std::string_view)>;
    using Sound=std::function<void(SelectionSound)>;
    SceneSelection(Loader,Text,Sound sound={});
    void settings(SelectionSettings);
    void replay_mode(bool value)noexcept{settings_.replay=value;}
    void sync(const SceneChoices&);
    void update(const std::function<std::int32_t(std::int32_t)>& duration={});
    std::optional<std::uint32_t> input(SelectionInput);
    std::span<const image::Sprite> sprites();
    std::span<const SelectionRow> rows()const noexcept{return rows_;}
    std::optional<std::uint32_t> focused()const noexcept{return focused_;}
    bool ready()const noexcept{return active_ && ready_;}
    bool loading()const noexcept{return active_ && (!images_[0] || (needs_marker_ && !images_[1]));}
    std::uint8_t opacity()const noexcept{return opacity_;}
private:
    Loader loader_;Text text_;Sound sound_;
    SelectionSettings settings_;
    std::optional<std::uint64_t> generation_;
    const SceneChoices* owner_{};
    std::vector<SelectionRow> rows_;
    std::vector<text::PreparedText> labels_;
    std::array<std::shared_ptr<image::ImageRequest>,2> requests_;
    std::array<std::shared_ptr<const image::Raster>,2> images_;
    std::vector<image::Sprite> sprites_;
    std::optional<std::uint32_t> focused_;
    std::uint32_t elapsed_{};
    std::uint8_t opacity_{};
    bool active_{},ready_{},needs_marker_{};
    void clear();
};
}
