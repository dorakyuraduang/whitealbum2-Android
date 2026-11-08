#pragma once
#include "title_view.hpp"
#include "extras_catalogue.hpp"
#include "menu_timing.hpp"
#include "music_caption.hpp"

namespace wa2::game {
enum class ExtraGridKind {gallery,replay};
enum class ExtraGridPhase {idle,loading,opening,cards,page_out,page_in,full_loading,full_in,full,closing,closed};
struct ExtraGridRow {unsigned ordinal{},index{},thumbnail{},theme{};bool enabled{};std::vector<unsigned> variants;};
struct ExtraGridInput {script::SelectionInput selection;bool cancel{};int page_delta{};};
struct ExtraGridCommand {enum class Kind {leaving,close,replay};Kind kind;unsigned entry{};};
// Both extras share one resource/input/presentation implementation. A pending
// page is committed only after all of its visible images are available.
class ExtraGrid {
public:
    using Loader=script::SceneSelection::Loader;
    using Sound=std::function<void(audio::SystemCue)>;
    ExtraGrid(Loader,Sound={},CurrentMusic={});
    void prepare(ExtraGridKind,TitleContent,const progress::State&,unsigned page=0,MenuTiming={});
    void update(ExtraGridInput={});
    void clear();
    ExtraGridKind kind()const noexcept{return kind_;}
    ExtraGridPhase phase()const noexcept{return phase_;}
    unsigned page()const noexcept{return page_;}
    std::span<const unsigned> pages()const noexcept{return pages_;}
    std::span<const ExtraGridRow> rows()const noexcept{return data_.rows;}
    std::span<const image::Sprite> sprites()const noexcept{return sprites_;}
    float visibility()const noexcept;
    std::optional<unsigned> displayed_variant()const noexcept{return full_?std::optional(full_id_):std::nullopt;}
    std::optional<ExtraGridCommand> take_command();
private:
    struct Asset {std::string name;std::shared_ptr<const image::Raster> pixels;};
    struct PageData {unsigned page{};std::vector<ExtraGridRow> rows;std::vector<Asset> assets;};
    struct Pending {PageData data;unsigned next{};std::shared_ptr<image::ImageRequest> request;};
    Loader loader_;Sound sound_;CurrentMusic current_music_;ExtraGridKind kind_{ExtraGridKind::gallery};TitleContent content_;
    bool coda_complete_{};
    std::array<std::array<bool,10>,168> gallery_{};std::array<bool,24> replay_{};
    std::vector<unsigned> pages_;PageData data_;std::optional<Pending> pending_;
    std::shared_ptr<const image::Raster> black_,full_,previous_full_;
    std::shared_ptr<image::ImageRequest> full_request_;
    std::vector<unsigned> variants_;unsigned variant_{},pending_variant_{},full_id_{},page_{},full_row_{};
    std::array<unsigned,2> full_view_{},previous_view_{};
    std::optional<unsigned> focus_;std::optional<ExtraGridCommand> command_;
    std::optional<ExtraGridCommand> leaving_;
    MenuTiming timing_;unsigned elapsed_{};float close_from_{1};int page_direction_{};
    std::vector<image::Sprite> sprites_;ExtraGridPhase phase_{ExtraGridPhase::idle};
    PageData plan(unsigned)const;
    void begin_page(unsigned,int direction=0);bool poll();void commit_page();void turn(int);void activate(unsigned);
    void leave(ExtraGridCommand);void finish_close();
    void full(unsigned);void cards();void draw();void pan(const script::SelectionInput&);
};
}
