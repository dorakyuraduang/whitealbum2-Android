#pragma once
#include "progress.hpp"
#include "scene_selection.hpp"
#include "scene_weather.hpp"
#include "system_sounds.hpp"

namespace wa2::game {
enum class TitleSection {root,chapters,extras,special_stories};
enum class TitlePhase {loading,background_in,logo_in,menu,story_out,quit_out,modal,finished};
// These describe available content, not a platform-specific menu. An external
// installation may provide IC through the same resource catalogue as local IC.
struct TitleContent {bool local_introductory{},external_introductory{},perspective_snow{true};};
struct TitleCommand {
    enum class Kind {start,load,options,extra,quit};
    Kind kind;std::string scene;std::int32_t extra{-1};
};
struct TitleInput {script::SelectionInput selection;bool cancel{};};
struct TitleRow {std::uint32_t ordinal{};std::int32_t cell{};float y{},hit_width{},hit_height{32};};
// Original root/chapter/extras title presentation in ordinary shared game
// state. Loading is asynchronous; update() advances exactly one logic tick.
// The caller handles emitted commands and supplies its existing audio services.
class TitleView {
public:
    using Loader=script::SceneWeather::Loader;
    using Random=script::SceneWeather::Random;
    using Sound=std::function<void(audio::SystemCue)>;
    using Music=std::function<void(std::optional<std::int32_t> song,std::int32_t fade)>;
    TitleView(Loader,Random,Sound={},Music={});
    void prepare(TitleContent,const progress::State&);
    void update(TitleInput={});
    void resume(const progress::State&);
    void clear();
    std::optional<TitleCommand> take_command();
    std::span<const image::Sprite> sprites()const noexcept{return sprites_;}
    std::span<const TitleRow> rows()const noexcept{return rows_;}
    TitlePhase phase()const noexcept{return phase_;}
    TitleSection section()const noexcept{return section_;}
    std::optional<std::uint32_t> focused()const noexcept{return focus_;}
    std::uint32_t elapsed()const noexcept{return elapsed_;}
    float pan()const noexcept{return pan_;}
    bool loading()const noexcept{return phase_==TitlePhase::loading;}
    const script::SceneWeather& weather()const noexcept{return weather_;}
private:
    Loader loader_;Sound sound_;Music music_;
    script::SceneWeather weather_;
    TitleContent content_;
    std::array<std::shared_ptr<image::ImageRequest>,8> requests_;
    std::array<std::shared_ptr<const image::Raster>,8> images_;
    std::vector<TitleRow> rows_;
    std::vector<image::Sprite> sprites_;
    TitlePhase phase_{TitlePhase::finished};TitleSection section_{TitleSection::root};
    std::optional<std::uint32_t> focus_;
    std::optional<TitleCommand> command_,leaving_;
    std::uint32_t elapsed_{};float pan_{};
    bool introductory_clear_{},closing_clear_{},coda_unlocked_{},all_clear_{};
    bool music_started_{},prepared_{};
    void access(const progress::State&);
    bool poll();
    void menu(TitleSection,std::uint32_t elapsed=0);
    void input(TitleInput);
    void activate(std::uint32_t);
    void leave(TitleCommand);
    void emit(TitleCommand);
    void draw();
    void snow();
};
}
