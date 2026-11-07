#pragma once
#include "title_view.hpp"
#include "menu_timing.hpp"
#include "music_caption.hpp"

namespace wa2::game {
struct MusicRow {unsigned ordinal{},index{};std::int32_t song{};bool enabled{};};
struct MusicInput {script::SelectionInput selection;bool cancel{};int page_delta{};};
struct MusicCommand {enum class Kind {play,stop,leaving,close};Kind kind;unsigned index{};std::int32_t song{},fade{};};
enum class MusicViewPhase {idle,loading,opening,active,page_out,page_in,closing,closed};
// Cached source 0x428400: two ordinary menu pages, original atlas cells and
// progress flags. The audio owner consumes commands and acknowledges success.
class MusicView {
public:
    using Loader=script::SceneSelection::Loader;
    using Sound=std::function<void(audio::SystemCue)>;
    MusicView(Loader,Sound={},CurrentMusic={});
    void prepare(TitleContent,const progress::State&,MenuTiming={});
    void update(MusicInput={});
    void clear();
    void played(std::optional<unsigned>);
    MusicViewPhase phase()const noexcept{return phase_;}
    unsigned page()const noexcept{return page_;}
    std::span<const MusicRow> rows()const noexcept{return rows_;}
    std::span<const image::Sprite> sprites()const noexcept{return sprites_;}
    std::optional<unsigned> playing()const noexcept{return playing_;}
    std::optional<unsigned> focused()const noexcept{return focus_;}
    float visibility()const noexcept;
    std::optional<MusicCommand> take_command();
private:
    Loader loader_;Sound sound_;CurrentMusic current_music_;TitleContent content_;std::array<bool,200> unlocked_{};
    std::array<std::shared_ptr<const image::Raster>,11> images_;
    std::shared_ptr<const image::Raster> black_;
    std::shared_ptr<image::ImageRequest> request_;
    std::vector<MusicRow> rows_;std::vector<image::Sprite> sprites_;
    std::optional<unsigned> focus_,playing_;std::optional<MusicCommand> command_;
    MusicViewPhase phase_{MusicViewPhase::idle};unsigned page_{},image_next_{};
    MenuTiming timing_;unsigned elapsed_{};float close_from_{1};
    bool images();void rebuild_rows();void turn();void activate(unsigned);void draw();
};
}
