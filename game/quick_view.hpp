#pragma once
#include "confirmation_view.hpp"
#include "menu_timing.hpp"

namespace wa2::game {
enum class QuickKind {save,load};
enum class QuickPhase {idle,loading,confirming,awaiting,result,closing,closed};
enum class QuickCommand {load,close};
struct QuickContext {QuickKind kind{QuickKind::save};int skin{};bool confirmation{true};MenuTiming timing;unsigned result_ticks{60};};
// Source quick-save notification and quick-load confirmation share their
// ordinary native scene owner. No snapshot of the desktop enters this view.
class QuickView {
public:
    using Loader=script::SceneSelection::Loader;
    using Text=std::function<text::PreparedText(std::string_view,const text::TextStyle&)>;
    using Sound=std::function<void(audio::SystemCue)>;
    QuickView(Loader,Text,Sound={});
    void prepare(QuickContext);
    void update(ConfirmationInput={});
    void succeeded();
    void clear();
    QuickPhase phase()const noexcept{return phase_;}
    const ConfirmationView* confirmation()const noexcept{return confirmation_.get();}
    std::span<const image::Sprite> sprites()const noexcept{return sprites_;}
    std::optional<QuickCommand> take_command();
private:
    Loader loader_;Text text_;Sound sound_;
    QuickContext context_;
    std::shared_ptr<image::ImageRequest> request_;
    std::shared_ptr<const image::Raster> result_image_,black_;
    std::unique_ptr<ConfirmationView> confirmation_;
    text::PreparedText label_;
    std::vector<image::Sprite> sprites_;
    std::optional<QuickCommand> command_;
    QuickPhase phase_{QuickPhase::idle};
    unsigned elapsed_{};bool committed_{};
    void closed();
    void result();
    void draw();
};
std::string_view quick_load_prompt()noexcept;
std::string_view quick_load_result_prompt()noexcept;
}
