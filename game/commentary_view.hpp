#pragma once
#include "scene_selection.hpp"
#include "system_sounds.hpp"
#include "menu_timing.hpp"

namespace wa2::game {
enum class CommentaryPhase {idle,loading,opening,menu,text_loading,listening,closing,closed};
struct CommentaryInput {script::SelectionInput selection;bool cancel{};};
struct CommentaryCommand {enum class Kind {play,stop,leaving,close};Kind kind;unsigned speaker{};};
// The cached five-column voice-comment extra is an ordinary native page. Its
// images remain external and its audio owner acknowledges/finishes playback.
class CommentaryView {
public:
    using Loader=script::SceneSelection::Loader;using Sound=std::function<void(audio::SystemCue)>;
    CommentaryView(Loader,Sound={});
    void prepare(MenuTiming={});void update(CommentaryInput={},bool playing=true);void clear();void failed();
    CommentaryPhase phase()const noexcept{return phase_;}
    std::span<const image::Sprite> sprites()const noexcept{return sprites_;}
    float visibility()const noexcept;
    std::optional<CommentaryCommand> take_command();
    std::optional<unsigned> speaker()const noexcept{return phase_==CommentaryPhase::listening?std::optional(speaker_):std::nullopt;}
private:
    Loader loader_;Sound sound_;std::array<std::shared_ptr<const image::Raster>,6> images_;
    std::shared_ptr<image::ImageRequest> request_;std::shared_ptr<const image::Raster> page_;
    std::optional<unsigned> focus_;std::optional<CommentaryCommand> command_;
    CommentaryPhase phase_{CommentaryPhase::idle};unsigned next_{},speaker_{};
    MenuTiming timing_;unsigned elapsed_{};float close_from_{1};
    std::vector<image::Sprite> sprites_;bool images();void activate(unsigned);void draw();
};
}
