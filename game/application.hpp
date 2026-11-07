#pragma once
#include "scene.hpp"
#include "save_slots.hpp"
#include "title_scene.hpp"
#include "options_preferences.hpp"
#include "save_view.hpp"
#include "backlog_view.hpp"

namespace wa2::game {
enum class Page {title,story,options,save,load,backlog,extras,closed};
struct StoryStart {std::string scene{"1001"};std::int32_t label{};bool replay{};};
struct SaveStamp {SaveDate date;std::uint64_t milliseconds{};};
struct SaveServices {
    SaveView::Loader images;
    SaveView::Text text;
    std::function<SaveStamp()> stamp;
    std::function<std::shared_ptr<const image::Raster>(Scene&)> thumbnail;
    std::function<void()> worker;
};
// Shared application ownership/navigation. Hosts provide resources, storage,
// input, clocks, presentation and device audio; they do not implement another
// version of the story/save/menu transition rules. Page views are separate.
class Application {
    resource::BankOpener opener_;
    SceneServices services_;
    std::shared_ptr<progress::Store> progress_;
    PreferenceStore preferences_;
    SaveSlots saves_;
    Scene::SavedImageResolver resolver_;
    std::unique_ptr<Scene> scene_;
    struct TitleSetup {TitleContent content;TitleStartup startup;TitleServices services;};
    std::optional<TitleSetup> title_setup_;
    std::unique_ptr<TitleScene> title_scene_;
    OptionsView::Loader options_loader_;
    std::function<void(OptionsCommand)> options_command_;
    ConfirmationView::Text options_text_;
    std::unique_ptr<OptionsView> options_view_;
    SaveServices save_services_;
    std::unique_ptr<SaveView> save_view_;
    BacklogView::Loader backlog_loader_;
    BacklogView::Text backlog_text_;
    std::unique_ptr<BacklogView> backlog_view_;
    std::vector<script::BacklogVoice> replay_;
    std::size_t replay_next_{};
    std::optional<std::size_t> replay_entry_;
    std::function<void(bool)> display_;
    std::function<void(std::string_view)> error_;
    std::int32_t extra_{-1};
    std::vector<Page> pages_{Page::title};
    std::int64_t observed_{};
    bool clock_started_{},background_{},release_input_{true},recover_clock_{};
    std::uint32_t seed_;
    std::uint64_t scene_revision_{};
    std::uint64_t clock_revision_{};
    void observe(std::int64_t now);
    void require_open()const;
    void suspend_scene(std::int64_t now);
    void replace_scene(std::unique_ptr<Scene>,std::int64_t now);
    SaveDocument save_document();
    void close_backlog();
    void control(script::StoryControl,std::int64_t now);
    void report_error(std::string_view,std::int64_t now);
public:
    Application(resource::BankOpener,std::shared_ptr<progress::Store>,std::shared_ptr<PreferenceStorage>,std::shared_ptr<SaveStorage>,Scene::SavedImageResolver,std::uint32_t seed=1,SceneServices services={});
    Page page()const noexcept{return pages_.back();}
    std::span<const Page> navigation()const noexcept{return pages_;}
    Scene* scene()noexcept{return scene_.get();}
    const Scene* scene()const noexcept{return scene_.get();}
    std::uint64_t scene_revision()const noexcept{return scene_revision_;}
    std::uint64_t clock_revision()const noexcept{return clock_revision_;}
    std::shared_ptr<audio::Mixer> mixer()const noexcept{return scene_?scene_->mixer():title_scene_?title_scene_->mixer():nullptr;}
    const TitleScene* title_scene()const noexcept{return title_scene_.get();}
    const OptionsView* options_view()const noexcept{return options_view_.get();}
    const SaveView* save_view()const noexcept{return save_view_.get();}
    const BacklogView* backlog_view()const noexcept{return backlog_view_.get();}
    text::PreparedText menu_text(std::string_view,const text::TextStyle&);
    std::int32_t extra()const noexcept{return extra_;}
    bool background()const noexcept{return background_;}
    const Preferences& preferences()const noexcept{return preferences_.current();}
    bool preferences(const Preferences&);
    void prepare_display(std::function<void(bool)>);
    void prepare_errors(std::function<void(std::string_view)>);
    void prepare_title(TitleContent,TitleStartup={},TitleServices={});
    // Wiring is separate from opening: constructing the application does not
    // decode menu assets. Reset/preview owners remain explicit integration work.
    void prepare_options(OptionsView::Loader,std::function<void(OptionsCommand)> = {},ConfirmationView::Text = {});
    void prepare_saves(SaveServices);
    void prepare_backlog(BacklogView::Loader,BacklogView::Text);
    void prepare_save_thumbnail(std::function<std::shared_ptr<const image::Raster>(Scene&)>);
    bool has_save_thumbnail()const noexcept{return bool(save_services_.thumbnail);}
    void start(const StoryStart&,std::int64_t now);
    void open(Page,std::int64_t now);
    bool back(std::int64_t now);
    void title(std::int64_t now);
    void background(bool,std::int64_t now);
    void save(unsigned slot);
    void load(unsigned slot,std::int64_t now);
    std::vector<SaveSlotInfo> slots(){require_open();return saves_.list();}
    FrameBatch frame(std::int64_t now,Input,const std::function<std::int64_t()>& budget_clock,FrameBudget budget={});
    void close(std::int64_t now);
};
}
