#pragma once
#include "scene_calendar.hpp"
#include "resource_catalog.hpp"
#include "native_random.hpp"
#include "script_session.hpp"
#include "scene_background.hpp"
#include "scene_bitmaps.hpp"
#include "scene_colour.hpp"
#include "scene_characters.hpp"
#include "scene_controls.hpp"
#include "scene_progress.hpp"
#include "scene_choices.hpp"
#include "scene_selection.hpp"
#include "scene_read_history.hpp"
#include "scene_builtins.hpp"
#include "scene_dialogue.hpp"
#include "scene_music.hpp"
#include "scene_effects.hpp"
#include "scene_voices.hpp"
#include "scene_movie.hpp"
#include "scene_weather.hpp"
#include "scene_shake.hpp"
#include "scene_captions.hpp"
#include "system_sounds.hpp"
#include "timeline.hpp"
#include "frame_batch.hpp"
#include "scene_backlog.hpp"
#include "preferences.hpp"

namespace wa2::game {
struct Input {bool advance{},skip{},toggle_window{};std::int32_t choice{-1};script::SelectionInput selection;
    bool cancel{},pointer_held{};std::int32_t page_delta{};
    Input continued()const{auto next=*this;next.advance=next.toggle_window=next.cancel=false;next.choice=-1;next.selection.activate=next.selection.confirm=false;next.selection.navigation=next.page_delta=0;return next;}
};
struct Statistics {std::uint64_t ticks{},messages{},voices{};std::uint32_t command{};std::string scene;};
// Optional transport providers, used without changing common game policy.
// Default providers decode the same external PAK members on every host.
// State checks inject finite fakes instead of opening device/codec transports.
struct SceneServices {
    script::SceneMusic::Factory music;
    script::SceneEffects::PreparedFactory effects;
    script::SceneVoices::Factory speech;
    script::SceneMovie::Factory movie;
};
// Shared native migration core; not yet the released app's game entry.
// It contains ordinary game state and the original LSCR language, no CPU state,
// original machine-code image, Windows imports, or instruction dispatcher.
class Scene {
    resource::Catalog resources_;
    SceneServices services_;
    std::shared_ptr<audio::Mixer> mixer_;
    audio::SystemSounds system_sounds_;
    text::FontBook fonts_;
    script::Library library_;
    script::Session session_;
    wa2::Random random_;
    std::shared_ptr<progress::Store> progress_store_;
    script::SceneReadHistory reading_;
    script::SceneBuiltins builtins_;
    std::uint32_t milliseconds_{};
    Timeline timeline_;
    Input input_;
    Statistics statistics_;
    Preferences preferences_;
    bool background_changing_{},preserve_characters_{},window_visible_{true},title_requested_{},skip_read_only_{true},replay_mode_{};
    script::SceneControls controls_;
    script::SceneProgress progress_;
    script::SceneChoices choices_;
    script::SceneSelection selection_;
    script::SceneCalendar calendar_;
    script::SceneCaptions captions_;
    script::SceneBacklog backlog_;
    script::SceneMusic music_;
    script::SceneEffects effects_;
    script::SceneVoices voices_;
    script::SceneWeather weather_;
    script::SceneShake shake_;
    script::SceneBackground backgrounds_;
    script::SceneBitmaps bitmaps_;
    script::SceneCharacters characters_;
    script::SceneColour colour_;
    script::SceneDialogue dialogue_;
    script::SceneMovie movies_;
    std::vector<image::Sprite> environment_,foreground_;
    std::unique_ptr<audio::Voice> sound(std::int32_t id,bool loop,bool paused=false);
    audio::SoundResource sound_resource(std::int32_t id);
    std::unique_ptr<audio::Voice> music(std::int32_t id,bool loop);
    std::unique_ptr<audio::Voice> speech(const script::SpokenLine& line);
    script::CommandResult command(script::Command& command);
    void choose(std::uint32_t ordinal);
    bool skipping()const{return !timeline_.paused() && (input_.skip || controls_.modes().latched_skip) && !controls_.modes().demo_mode && !choices_.active() && (!skip_read_only_ || reading_.status(replay_mode_)!=0);}
public:
    explicit Scene(resource::BankOpener opener,std::uint32_t seed=1,std::shared_ptr<progress::Store> progress={},SceneServices services={});
    void start(std::string_view scene="1001",std::int32_t label=0);
    script::RunState update(std::int64_t monotonic_us,Input input={});
    FrameBatch advance_frame(std::int64_t monotonic_us,Input input,const std::function<std::int64_t()>& budget_clock,FrameBudget budget={});
    void pause(bool paused,std::int64_t monotonic_us);
    void suspend(bool logic_paused,bool audio_paused,std::int64_t monotonic_us);
    void ambient_update(std::int64_t monotonic_us);
    void stop_movie(){movies_.stop();}
    std::shared_ptr<audio::Mixer> mixer()const noexcept{return mixer_;}
    script::BackgroundFrame background()const;
    std::span<const image::Sprite> environment();
    std::span<const image::Sprite> foreground();
    std::span<const image::Sprite> dialogue(){return dialogue_.sprites();}
    const image::SceneTransform& dialogue_transform()const noexcept{return shake_.dialogue();}
    std::optional<movie::VideoFrame> movie()const{return movies_.frame();}
    bool movie_active()const{return movies_.active();}
    const Statistics& statistics()const noexcept{return statistics_;}
    std::uint32_t random_state()const noexcept{return random_.state();}
    const script::SceneDialogue& dialogue_state()const noexcept{return dialogue_;}
    const script::SceneChoices& choices()const noexcept{return choices_;}
    const script::SceneBacklog& backlog()const noexcept{return backlog_;}
    std::span<const image::Sprite> selection(){return selection_.sprites();}
    std::span<const image::Sprite> calendar(){return calendar_.sprites();}
    std::span<const image::Sprite> captions(){return preferences_.captions?captions_.sprites():std::span<const image::Sprite>{};}
    bool title_requested()const noexcept{return title_requested_;}
    void skip_read_only(bool value){skip_read_only_=preferences_.skip_read_only=value;}
    void preferences(const Preferences&);
    void cue(audio::SystemCue value){system_sounds_.play(value);}
    void options_preview_effects();
    void options_preview_voice();
    void options_reset_sound();
    const Preferences& preferences()const noexcept{return preferences_;}
    text::PreparedText menu_text(std::string_view raw,const text::TextStyle& style){return fonts_.prepare(text::layout(raw,style));}
    std::uint32_t save_story_code()const;
    std::int32_t theme()const;
    void replay_mode(bool value){replay_mode_=value;selection_.replay_mode(value);}
    void flush_progress(){reading_.finish();}
    std::vector<std::uint8_t> checkpoint()const;
    using SavedImageResolver=std::function<image::Resolve(resource::Catalog&)>;
    static std::unique_ptr<Scene> restore(resource::BankOpener,std::shared_ptr<progress::Store>,std::span<const std::uint8_t>,std::int64_t monotonic_us,const SavedImageResolver&,const Preferences& preferences={},SceneServices services={});
private:
    bool can_accelerate()const;
};
}
