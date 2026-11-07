#pragma once
#include "scene_selection.hpp"
#include "system_sounds.hpp"

namespace wa2::game {
enum class ConfirmationPhase {idle,loading,opening,active,closing,closed};
struct ConfirmationInput {script::SelectionInput selection;bool cancel{};};
// The original two-button modal. Labels remain original font-atlas bytes;
// platform dialogs and Unicode fonts do not enter this presentation path.
class ConfirmationView {
public:
    using Loader=script::SceneSelection::Loader;
    using Text=script::SceneSelection::Text;
    using Sound=std::function<void(audio::SystemCue)>;
    ConfirmationView(Loader,Text,Sound={});
    void prepare(std::string_view prompt,int skin=0,unsigned opening=15,unsigned closing=15);
    void update(ConfirmationInput={});
    ConfirmationPhase phase()const noexcept{return phase_;}
    std::optional<unsigned> focused()const noexcept{return focus_;}
    std::span<const image::Sprite> sprites()const noexcept{return sprites_;}
    std::optional<bool> take_answer();
private:
    Loader loader_;Text text_;Sound sound_;
    text::PreparedText label_;
    std::array<std::shared_ptr<const image::Raster>,2> images_;
    std::shared_ptr<const image::Raster> black_;
    std::shared_ptr<image::ImageRequest> request_;
    std::vector<image::Sprite> sprites_;
    std::optional<unsigned> focus_,pressed_;
    std::optional<bool> answer_;
    ConfirmationPhase phase_{ConfirmationPhase::idle};
    unsigned elapsed_{},open_{},close_{};
    bool poll();
    void choose(bool);
    void draw();
};
std::string_view options_reset_prompt()noexcept;
}
