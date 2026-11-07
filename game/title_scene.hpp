#pragma once
#include "title_view.hpp"
#include "scene_music.hpp"
#include "scene_effects.hpp"
#include "scene_voices.hpp"
#include "movie_player.hpp"
#include "preferences.hpp"
#include "resource_catalog.hpp"
#include "timeline.hpp"

namespace wa2::game {
// The authorized resource namespace replaces the original installation
// registry. The cached local-IC test is existence of IC/bak.pak (0x432ab1).
// A provider failure must propagate rather than be treated as missing content.
TitleContent installed_title_content(const resource::BankOpener&);
struct TitleStartup {bool movies{true};unsigned movie_suffix{};};
enum class TitleStage {company_movie,opening_delay,opening_movie,view};
struct TitleServices {
    script::SceneMusic::Factory music;
    std::function<std::unique_ptr<movie::Playback>(std::string_view)> movie;
    script::SceneEffects::Factory effects;
    script::SceneVoices::Factory speech;
};
// Shared title ownership, startup movies and audio. Device output may keep a
// shared mixer alive while switching owners; no OS or device API is used here.
class TitleScene {
    resource::Catalog resources_;
    std::shared_ptr<audio::Mixer> mixer_;
    std::shared_ptr<progress::Store> progress_;
    audio::SystemSounds sounds_;
    script::SceneMusic music_;
    script::SceneEffects effects_;
    script::SceneVoices voices_;
    wa2::Random random_;
    TitleView view_;
    std::unique_ptr<movie::Playback> movie_;
    TitleServices services_;
    Preferences preferences_;
    TitleContent content_;
    TitleStartup startup_;
    TitleStage stage_;
    Timeline timeline_;
    unsigned delay_{};
    void begin_movie(std::string_view stem);
    void begin_view();
public:
    TitleScene(resource::BankOpener,std::shared_ptr<progress::Store>,const Preferences&,TitleContent,TitleStartup={},std::uint32_t seed=1,TitleServices={});
    ~TitleScene();
    void update(std::int64_t now,TitleInput={});
    void pause(bool,std::int64_t now);
    void preferences(const Preferences&);
    void resume(){view_.resume(progress_->state());}
    void cue(audio::SystemCue value){sounds_.play(value);}
    void options_preview_effects();
    void options_preview_voice();
    void options_reset_sound();
    std::shared_ptr<audio::Mixer> mixer()const noexcept{return mixer_;}
    TitleStage stage()const noexcept{return stage_;}
    const TitleView& view()const noexcept{return view_;}
    std::span<const image::Sprite> sprites()const noexcept{return stage_==TitleStage::view?view_.sprites():std::span<const image::Sprite>{};}
    std::optional<movie::VideoFrame> movie()const{return movie_?movie_->frame():std::nullopt;}
    std::optional<TitleCommand> take_command(){return stage_==TitleStage::view?view_.take_command():std::optional<TitleCommand>{};}
    std::uint32_t random_state()const noexcept{return random_.state();}
    bool paused()const noexcept{return timeline_.paused();}
};
}
