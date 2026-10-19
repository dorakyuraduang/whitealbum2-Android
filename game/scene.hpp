#pragma once
#include "resource_catalog.hpp"
#include "native_random.hpp"
#include "script_session.hpp"
#include "scene_background.hpp"
#include "scene_bitmaps.hpp"
#include "scene_colour.hpp"
#include "scene_characters.hpp"
#include "scene_controls.hpp"
#include "scene_dialogue.hpp"
#include "scene_music.hpp"
#include "scene_effects.hpp"
#include "scene_voices.hpp"
#include "scene_movie.hpp"
#include "scene_weather.hpp"

namespace wa2::game {
struct Input {bool advance{},skip{},toggle_window{};};
struct Statistics {std::uint64_t ticks{},messages{},voices{};std::uint32_t command{};std::string scene;};
// The desktop memory check and Android app use this same native scene host.
// It contains ordinary game state and the original LSCR language, no CPU state,
// original machine-code image, Windows imports, or instruction dispatcher.
class Scene {
    resource::Catalog resources_;
    std::shared_ptr<audio::Mixer> mixer_;
    text::FontBook fonts_;
    script::Library library_;
    script::Session session_;
    wa2::Random random_;
    std::uint32_t milliseconds_{};
    Input input_;
    Statistics statistics_;
    bool background_changing_{},window_visible_{true};
    script::SceneControls controls_;
    script::SceneMusic music_;
    script::SceneEffects effects_;
    script::SceneVoices voices_;
    script::SceneWeather weather_;
    script::SceneBackground backgrounds_;
    script::SceneBitmaps bitmaps_;
    script::SceneCharacters characters_;
    script::SceneColour colour_;
    script::SceneDialogue dialogue_;
    script::SceneMovie movies_;
    std::vector<image::Sprite> environment_,foreground_;
    std::unique_ptr<audio::Voice> sound(std::int32_t id,bool loop,bool paused=false);
    std::unique_ptr<audio::Voice> music(std::int32_t id,bool loop);
    std::unique_ptr<audio::Voice> speech(const script::SpokenLine& line);
    script::CommandResult command(script::Command& command);
    bool skipping()const noexcept{return input_.skip;}
public:
    explicit Scene(resource::BankOpener opener,std::uint32_t seed=1);
    void start(std::string_view scene="1001",std::int32_t label=0);
    script::RunState update(std::int64_t monotonic_us,Input input={});
    void pause(bool paused,std::int64_t monotonic_us){movies_.pause(paused,monotonic_us);}
    void stop_movie(){movies_.stop();}
    std::shared_ptr<audio::Mixer> mixer()const noexcept{return mixer_;}
    script::BackgroundFrame background()const;
    std::span<const image::Sprite> environment();
    std::span<const image::Sprite> foreground();
    std::span<const image::Sprite> dialogue(){return dialogue_.sprites();}
    std::optional<movie::VideoFrame> movie()const{return movies_.frame();}
    bool movie_active()const{return movies_.active();}
    const Statistics& statistics()const noexcept{return statistics_;}
    const script::SceneDialogue& dialogue_state()const noexcept{return dialogue_;}
};
}
