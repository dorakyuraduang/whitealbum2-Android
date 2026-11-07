#pragma once
#include "scene_backlog.hpp"
#include "scene_selection.hpp"
#include "system_sounds.hpp"

namespace wa2::game {
struct BacklogContext {int skin{};bool novel{};std::uint32_t current_page{};};
struct BacklogInput {script::SelectionInput selection;bool cancel{},held{};int page_delta{},scroll{};};
struct BacklogHit {unsigned id{};float x{},y{},width{},height{};bool enabled{true};};
struct BacklogCommand {enum class Kind {close,replay};Kind kind;std::size_t entry{};};
enum class BacklogPhase {idle,loading,active,closed};
class BacklogView {
public:
    using Loader=script::SceneSelection::Loader;
    using Text=std::function<text::PreparedText(std::string_view,const text::TextStyle&)>;
    using Sound=std::function<void(audio::SystemCue)>;
    BacklogView(Loader,Text,Sound={});
    void prepare(BacklogContext,const script::SceneBacklog&);
    void update(BacklogInput={});
    void clear();
    BacklogPhase phase()const noexcept{return phase_;}
    std::size_t position()const noexcept{return position_;}
    std::size_t maximum()const noexcept;
    std::span<const image::Sprite> sprites()const noexcept{return sprites_;}
    std::span<const BacklogHit> hits()const noexcept{return hits_;}
    const std::deque<script::BacklogEntry>& entries()const noexcept{return entries_;}
    std::optional<BacklogCommand> take_command();
    void playing(std::optional<std::size_t> entry){playing_=entry;}
private:
    struct Row {std::size_t entry{};text::PreparedText name,body;std::uint32_t page{};};
    Loader loader_;Text text_;Sound sound_;
    BacklogContext context_;
    std::deque<script::BacklogEntry> entries_;
    std::vector<Row> rows_;
    std::array<std::shared_ptr<const image::Raster>,6> images_;
    std::shared_ptr<const image::Raster> black_;
    std::shared_ptr<image::ImageRequest> request_;
    std::vector<image::Sprite> sprites_;
    std::vector<BacklogHit> hits_;
    std::optional<unsigned> focus_;
    std::optional<std::size_t> playing_;
    std::optional<BacklogCommand> command_;
    BacklogPhase phase_{BacklogPhase::idle};
    std::size_t position_{};unsigned image_next_{};bool dragging_{};
    std::string name(unsigned)const;
    std::vector<Row> rows(std::size_t,const std::deque<script::BacklogEntry>&,BacklogContext)const;
    void move(std::size_t);
    bool images();
    void regions();
    void activate(unsigned);
    void draw();
};
}
