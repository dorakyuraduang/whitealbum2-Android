#pragma once
#include "scene_selection.hpp"
#include "system_sounds.hpp"

namespace wa2::game {
enum class Option : unsigned {
    display,text_speed,skip_policy,auto_delay,transition_wait,
    window_opacity,picture_opacity,novel_opacity,button_opacity,window_visibility,
    confirmation,right_click,master_volume,music_volume,effects_volume,voice_volume,voice_wait,adult_voice,
    character_0,character_2,character_1,character_3,character_4,character_5,character_10,character_11,character_99,character_98,count
};
// A view model, not another preferences file. Choice values are atlas cell
// indices; sliders contain source units (0..256, or 60..600 for auto delay).
// The application binds these edits to preferences and platform services.
struct OptionsState {
    std::array<std::int32_t,unsigned(Option::count)> values{
        1,2,0,137,1,207,134,134,134,0,
        0,0,195,134,195,195,1,0,0,0,0,0,0,0,0,0,0,0};
    std::array<bool,4> muted{};
    std::int32_t value(Option)const;
    void value(Option,std::int32_t);
    void validate()const;
    bool operator==(const OptionsState&)const=default;
};
enum class OptionsPhase {idle,loading,opening,active,page_loading,page_out,page_in,closing,closed};
struct OptionsTiming {std::uint32_t open{15},page{7},close{15};};
struct OptionsInput {script::SelectionInput selection;bool cancel{},held{};std::int32_t page_delta{};};
struct OptionsHit {
    unsigned id{},row{},cell{};
    std::optional<Option> option;
    float x{},y{},width{},height{};
};
struct OptionsCommand {enum class Kind {close,reset,preview_effects,preview_voice};Kind kind;};
// Common source-resource settings presentation and input. No device/window
// calls or guest addresses. Commands which require confirmation/audio are
// delivered to the owner; unsupported owner behavior must remain explicit.
class OptionsView {
public:
    using Loader=script::SceneSelection::Loader;
    using Sound=std::function<void(audio::SystemCue)>;
    using Apply=std::function<void(const OptionsState&)>;
    OptionsView(Loader,Apply={},Sound={});
    void prepare(const OptionsState&,unsigned page=0,OptionsTiming={});
    void clear();
    void update(OptionsInput={});
    void state(const OptionsState&);
    const OptionsState& state()const noexcept{return state_;}
    OptionsPhase phase()const noexcept{return phase_;}
    unsigned page()const noexcept{return page_;}
    std::optional<unsigned> focused()const noexcept{return focus_;}
    std::span<const OptionsHit> hits()const noexcept{return hits_;}
    std::span<const image::Sprite> sprites()const noexcept{return sprites_;}
    std::optional<OptionsCommand> take_command();
private:
    using Image=std::shared_ptr<const image::Raster>;
    struct Bundle {std::array<Image,11> images;unsigned page{},next{};std::shared_ptr<image::ImageRequest> request;};
    Loader loader_;Apply apply_;Sound sound_;
    OptionsState state_;OptionsTiming timing_;
    Bundle assets_;std::optional<Bundle> pending_;
    std::shared_ptr<const image::Raster> black_;
    std::vector<OptionsHit> hits_;
    std::vector<image::Sprite> sprites_;
    std::optional<unsigned> focus_,drag_,pressed_;
    std::optional<OptionsCommand> command_;
    OptionsPhase phase_{OptionsPhase::idle};unsigned page_{},elapsed_{};
    bool poll();
    void regions();
    void change_page(unsigned);
    void input(OptionsInput);
    void activate(const OptionsHit&,float x);
    void publish(const OptionsState&);
    void emit(OptionsCommand::Kind);
    void draw();
};
}
