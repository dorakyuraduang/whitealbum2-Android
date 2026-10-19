#pragma once
#include "script_runtime.hpp"
#include "text_layout.hpp"
#include "image_store.hpp"

namespace wa2::script {
enum class DialoguePhase {closed,opening,typing,waiting,complete,closing};
struct DialogueSettings {
    int skin{},window_alpha{207},picture_alpha{134},button_alpha{134},text_step{10},open_ticks{10};
    bool window{true};
};
class SceneDialogue {
public:
    using Loader=std::function<std::shared_ptr<image::ImageRequest>(std::string_view)>;
    SceneDialogue(text::FontBook& fonts,Loader loader,std::function<void(std::int32_t)> seen={},std::function<void()> end_message={});
    std::optional<CommandResult> dispatch(Command& command);
    void update(bool fast_forward=false);
    void advance();
    void visible(bool value){visible_=value;}
    void settings(DialogueSettings value);
    void picture(bool value){picture_=value;}
    DialoguePhase phase()const noexcept{return phase_;}
    const std::string& raw_text()const noexcept{return raw_;}
    const std::string& name()const noexcept{return name_;}
    std::int32_t speaker()const noexcept{return speaker_;}
    std::size_t page()const noexcept{return page_;}
    std::uint32_t budget()const noexcept{return budget_;}
    bool ready()const noexcept{return assets_ready_;}
    bool complete()const noexcept{return phase_==DialoguePhase::complete;}
    const text::PreparedText& text()const noexcept{return body_;}
    std::span<const image::Sprite> sprites();
private:
    text::FontBook& fonts_;
    Loader loader_;
    std::function<void(std::int32_t)> seen_;
    std::function<void()> end_message_;
    DialogueSettings settings_;
    std::array<std::shared_ptr<image::ImageRequest>,4> requests_;
    std::array<std::shared_ptr<const image::Raster>,4> images_;
    text::PreparedText body_,name_text_,old_name_text_;
    std::vector<image::Sprite> sprites_;
    std::string raw_,name_;
    DialoguePhase phase_{DialoguePhase::closed};
    std::uint32_t budget_{},open_elapsed_{},name_elapsed_{},idle_elapsed_{};
    std::uint32_t close_ticks_{};
    std::size_t page_{};
    std::int32_t speaker_{-1},message_{-1};
    bool assets_started_{},assets_ready_{},visible_{true},picture_{},has_text_{},fast_{},transparent_{};
    void load_assets();
    void open();
    void name(std::string_view value,std::int32_t speaker);
    void message(std::string_view value,std::int32_t id,bool replace);
    std::uint32_t duration()const;
};
}
