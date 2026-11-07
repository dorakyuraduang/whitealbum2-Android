#pragma once
#include "confirmation_view.hpp"
#include "options_view.hpp"
#include "save_catalog.hpp"
#include "save_slots.hpp"

namespace wa2::game {
inline constexpr unsigned manual_slots=100,quick_slot=100,alternate_save_base=200;
enum class SaveMode {save,load};
enum class SavePhase {idle,loading,opening,active,page_loading,page_out,page_in,confirming,awaiting,result,closing,closed};
struct SaveContext {SaveMode mode{SaveMode::save};int theme{},skin{};bool title{},alternate{},confirmation{true};OptionsTiming timing;unsigned result_ticks{60};};
struct SaveInput {script::SelectionInput selection;bool cancel{};int page_delta{};};
struct SaveHit {unsigned id{};float x{},y{},width{},height{};bool enabled{true};};
struct SaveRow {unsigned slot{};bool occupied{};std::optional<SavePreview> preview;std::string diagnostic;};
struct SaveCommand {enum class Kind {save,load,close};Kind kind;unsigned slot{};};
class SaveView {
public:
    using Loader=script::SceneSelection::Loader;
    using Preview=std::function<std::shared_ptr<SavePreviewRequest>(unsigned)>;
    using Text=std::function<text::PreparedText(std::string_view,const text::TextStyle&)>;
    using Sound=std::function<void(audio::SystemCue)>;
    SaveView(Loader,Preview,Text,Sound={});
    void prepare(SaveContext,std::span<const SaveSlotInfo>);
    void update(SaveInput={});
    void clear();
    void succeeded(std::optional<SavePreview> saved={});
    void failed(std::string diagnostic);
    std::optional<SaveCommand> take_command();
    SavePhase phase()const noexcept{return phase_;}
    unsigned page()const noexcept{return page_;}
    unsigned bank()const noexcept{return bank_;}
    const SaveContext& context()const noexcept{return context_;}
    std::span<const SaveHit> hits()const noexcept{return hits_;}
    std::span<const SaveRow> rows()const noexcept{return rows_;}
    std::span<const image::Sprite> sprites()const noexcept{return sprites_;}
    const ConfirmationView* confirmation()const noexcept{return confirmation_.get();}
    const std::string& operation_error()const noexcept{return operation_error_;}
private:
    struct PageData {std::array<SaveRow,11> rows;std::array<text::PreparedText,11> labels;unsigned page{},bank{},next{};std::shared_ptr<SavePreviewRequest> request;};
    Loader loader_;Preview preview_;Text text_;Sound sound_;
    SaveContext context_;
    std::array<std::optional<SaveSlotInfo>,300> inventory_;
    std::array<bool,300> newly_saved_{};
    std::array<unsigned,2> pages_{},latest_{};
    std::array<std::shared_ptr<const image::Raster>,18> images_;
    std::shared_ptr<image::ImageRequest> image_request_;
    std::shared_ptr<const image::Raster> black_;
    std::optional<PageData> incoming_;
    PageData current_;
    std::array<SaveRow,10> rows_;
    std::vector<SaveHit> hits_;
    std::vector<image::Sprite> sprites_;
    std::unique_ptr<ConfirmationView> confirmation_;
    std::optional<unsigned> focus_,pressed_;
    std::optional<SaveCommand> command_,selected_;
    text::PreparedText result_label_;
    std::string operation_error_;
    SavePhase phase_{SavePhase::idle};unsigned page_{},bank_{},image_next_{},elapsed_{};
    std::string name(unsigned)const;
    bool images();
    PageData page_data(unsigned page,unsigned bank)const;
    bool poll(PageData&);
    void publish();
    void regions();
    void input(SaveInput);
    void change_page(unsigned,unsigned);
    void activate(unsigned);
    void emit(SaveCommand);
    void draw();
};
std::string_view save_overwrite_prompt()noexcept;
std::string_view save_load_prompt()noexcept;
std::string_view save_result_prompt()noexcept;
}
