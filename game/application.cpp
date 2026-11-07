#include "application.hpp"
#include <limits>

namespace wa2::game {
Application::Application(resource::BankOpener opener,std::shared_ptr<progress::Store> progress,std::shared_ptr<PreferenceStorage> preferences,std::shared_ptr<SaveStorage> saves,Scene::SavedImageResolver resolve,std::uint32_t seed,SceneServices services)
    :opener_(std::move(opener)),services_(std::move(services)),progress_(std::move(progress)),preferences_(std::move(preferences)),saves_(std::move(saves)),resolver_(std::move(resolve)),seed_(seed){
    if(!opener_ || !progress_ || !resolver_)throw std::invalid_argument("Missing native application resources, progress or image resolver");
}
void Application::require_open()const{if(page()==Page::closed)throw std::logic_error("Native application is closed");}
text::PreparedText Application::menu_text(std::string_view raw,const text::TextStyle& style){require_open();if(scene_)return scene_->menu_text(raw,style);if(title_scene_)return title_scene_->menu_text(raw,style);throw std::logic_error("Native menu text requires a prepared scene");}
void Application::observe(std::int64_t now){require_open();if(now<0 || (clock_started_ && now<observed_))throw std::invalid_argument("Native application clock moved backwards");observed_=now;clock_started_=true;}
void Application::suspend_scene(std::int64_t now){if(scene_)scene_->suspend(background_ || page()!=Page::story,background_,now);if(title_scene_)title_scene_->pause(background_,now);}
void Application::replace_scene(std::unique_ptr<Scene> next,std::int64_t now){
    if(scene_revision_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Native scene revision exhausted");
    next->suspend(background_,background_,now);if(scene_)scene_->flush_progress();save_view_.reset();seed_=next->random_state();scene_=std::move(next);title_scene_.reset();options_view_.reset();extra_=-1;pages_.resize(1);pages_[0]=Page::story;release_input_=true;++scene_revision_;
}
bool Application::preferences(const Preferences& next){require_open();const auto state=options_view_?std::optional(options_state(next)):std::nullopt;if(!preferences_.replace(next))return false;if(scene_)scene_->preferences(preferences_.current());if(title_scene_)title_scene_->preferences(preferences_.current());if(state){options_view_->state(*state);options_view_->timing(options_timing(next));}return true;}
void Application::prepare_options(OptionsView::Loader loader,std::function<void(OptionsCommand)> commands,ConfirmationView::Text text){
    require_open();if(!loader)throw std::invalid_argument("Missing native options image loader");if(options_view_)throw std::logic_error("Cannot replace native options services while their view is open");options_loader_=std::move(loader);options_command_=std::move(commands);options_text_=std::move(text);
}
void Application::prepare_saves(SaveServices services){
    require_open();if(!services.images || !services.text || !services.stamp)throw std::invalid_argument("Missing native save menu services");
    if(save_view_)throw std::logic_error("Cannot replace native save services while their view is open");
    if(!services.thumbnail)services.thumbnail=std::move(save_services_.thumbnail);save_services_=std::move(services);
}
void Application::prepare_save_thumbnail(std::function<std::shared_ptr<const image::Raster>(Scene&)> thumbnail){
    require_open();if(!thumbnail)throw std::invalid_argument("Missing native save thumbnail adapter");if(save_view_)throw std::logic_error("Cannot replace native thumbnail service while saving");save_services_.thumbnail=std::move(thumbnail);
}
SaveDocument Application::save_document(){
    if(!scene_ || !save_services_.stamp || !save_services_.thumbnail)throw script::Unsupported("Native save metadata or thumbnail adapter is not prepared");
    SaveDocument document;const auto stamp=save_services_.stamp();auto& preview=document.preview;preview.date=stamp.date;preview.saved_ms=stamp.milliseconds;
    preview.scene=scene_->statistics().scene;preview.description=save_description(scene_->dialogue_state().raw_text());preview.story_code=scene_->save_story_code();preview.story_known=true;
    preview.thumbnail=save_services_.thumbnail(*scene_);if(!preview.thumbnail)throw script::Error("Native save thumbnail adapter returned no image");preview.validate();document.snapshot=scene_->checkpoint();return document;
}
void Application::prepare_title(TitleContent content,TitleStartup startup,TitleServices services){
    require_open();if(page()!=Page::title || scene_)throw std::logic_error("Native title preparation requires the title page");
    const auto seed=title_scene_?title_scene_->random_state():seed_;auto next=std::make_unique<TitleScene>(opener_,progress_,preferences_.current(),content,startup,seed,services);next->pause(background_,observed_);
    title_setup_=TitleSetup{content,startup,std::move(services)};title_scene_=std::move(next);seed_=seed;extra_=-1;release_input_=true;
}
void Application::start(const StoryStart& start,std::int64_t now){
    observe(now);if(start.scene.empty() || start.scene.size()>240 || start.label<0)throw std::invalid_argument("Invalid native story entry");
    // Prepare completely before releasing the active owner or navigation.
    const auto seed=scene_?scene_->random_state():title_scene_?title_scene_->random_state():seed_;auto next=std::make_unique<Scene>(opener_,seed,progress_,services_);next->preferences(preferences_.current());next->replay_mode(start.replay);next->start(start.scene,start.label);replace_scene(std::move(next),now);
}
void Application::open(Page page,std::int64_t now){
    observe(now);if(page!=Page::options && page!=Page::save && page!=Page::load && page!=Page::backlog && page!=Page::extras)throw std::invalid_argument("Invalid native menu destination");
    if((page==Page::save || page==Page::backlog) && (!scene_ || pages_.front()!=Page::story))throw std::logic_error("This native menu requires an active story");
    if(page==Page::extras && pages_.front()!=Page::title)throw std::logic_error("Native extras require the title page");
    if(scene_ && scene_->movie_active())throw script::Unsupported("Native modal menu during movie playback is not implemented");
    if(page==this->page())return;if(pages_.size()>=8)throw std::logic_error("Native menu nesting exceeds its limit");
    std::unique_ptr<OptionsView> next;std::unique_ptr<SaveView> next_save;
    if(page==Page::options && options_loader_){
        if(options_view_ || (this->page()!=Page::story && this->page()!=Page::title))throw script::Unsupported("Native options over another modal page are not implemented");
        if(!scene_ && (!title_scene_ || title_scene_->stage()!=TitleStage::view))throw script::Unsupported("Native options require a prepared title view or story");
        next=std::make_unique<OptionsView>(options_loader_,[this](const OptionsState& state){const auto next=options_preferences(state,preferences());
            if(next.fullscreen!=preferences().fullscreen)throw script::Unsupported("Native options display-mode adapter is not implemented");
            if(next.right_click!=preferences().right_click && next.right_click==2 && !save_services_.images)throw script::Unsupported("Native right-click save-page services are not prepared");preferences(next);},
            [this](audio::SystemCue cue){if(scene_)scene_->cue(cue);else if(title_scene_)title_scene_->cue(cue);});
        next->prepare(options_state(preferences()),0,options_timing(preferences()));
    }
    if((page==Page::save || page==Page::load) && save_services_.images){
        if(save_view_ || (this->page()!=Page::story && this->page()!=Page::title))throw script::Unsupported("Native save menu over another modal page is not implemented");
        if(!scene_ && (!title_scene_ || title_scene_->stage()!=TitleStage::view))throw script::Unsupported("Native load menu requires a prepared title view or story");
        SaveContext context;context.mode=page==Page::save?SaveMode::save:SaveMode::load;context.title=!scene_;context.theme=scene_?scene_->theme():0;context.skin=preferences().skin;
        context.alternate=title_setup_ && title_setup_->content.local_introductory;context.confirmation=preferences().confirmation;context.timing=options_timing(preferences());
        next_save=std::make_unique<SaveView>(save_services_.images,[this](unsigned slot){return std::make_shared<SavePreviewRequest>([this,slot]{return saves_.preview(slot);},save_services_.worker);},save_services_.text,
            [this](audio::SystemCue cue){if(scene_)scene_->cue(cue);else if(title_scene_)title_scene_->cue(cue);});
        next_save->prepare(context,saves_.list());
    }
    pages_.push_back(page);release_input_=true;suspend_scene(now);
    if(next)options_view_=std::move(next);
    if(next_save)save_view_=std::move(next_save);
}
bool Application::back(std::int64_t now){observe(now);if(pages_.size()==1)return false;const auto leaving=page();pages_.pop_back();if(leaving==Page::options)options_view_.reset();if(leaving==Page::save || leaving==Page::load)save_view_.reset();if(page()==Page::title && title_scene_ && title_scene_->view().phase()==TitlePhase::modal)title_scene_->resume();release_input_=true;suspend_scene(now);return true;}
void Application::title(std::int64_t now){
    observe(now);const auto seed=scene_?scene_->random_state():title_scene_?title_scene_->random_state():seed_;std::unique_ptr<TitleScene> next;
    if(title_setup_){next=std::make_unique<TitleScene>(opener_,progress_,preferences_.current(),title_setup_->content,title_setup_->startup,seed,title_setup_->services);next->pause(background_,now);}
    if(scene_)scene_->flush_progress();save_view_.reset();scene_.reset();title_scene_=std::move(next);options_view_.reset();seed_=seed;extra_=-1;pages_.resize(1);pages_[0]=Page::title;release_input_=true;
}
void Application::background(bool value,std::int64_t now){observe(now);if(background_==value)return;background_=value;release_input_=true;suspend_scene(now);if(value)progress_->flush();}
void Application::save(unsigned slot){require_open();if(!scene_)throw std::logic_error("Native save requires an active story");saves_.write(slot,scene_->checkpoint());}
void Application::load(unsigned slot,std::int64_t now){
    observe(now);auto bytes=saves_.read(slot);if(!bytes)throw std::runtime_error("Native save slot is empty");auto next=Scene::restore(opener_,progress_,*bytes,now,resolver_,preferences_.current(),services_);replace_scene(std::move(next),now);
}
FrameBatch Application::frame(std::int64_t now,Input input,const std::function<std::int64_t()>& clock,FrameBudget budget){
    observe(now);
    if(input.cancel && pages_.size()>1 && !(page()==Page::options && options_view_) && !save_view_){back(now);input={};}
    if((page()==Page::save || page()==Page::load) && save_view_){
        if(scene_)scene_->ambient_update(now);if(title_scene_)title_scene_->update(now);
        if(background_)return {script::RunState::yielded,0};if(release_input_){input={};release_input_=false;}
        save_view_->update({input.selection,input.cancel || input.toggle_window,input.page_delta});
        if(auto command=save_view_->take_command()){
            if(command->kind==SaveCommand::Kind::close)back(now);
            else try{
                if(command->kind==SaveCommand::Kind::load)load(command->slot,now);
                else{auto document=save_document();saves_.write(command->slot,document);save_view_->succeeded(std::move(document.preview));}
            }catch(const std::exception& error){if(save_view_)save_view_->failed(error.what());throw;}
        }
        return {script::RunState::yielded,0};
    }
    if(page()==Page::options && options_view_){
        if(scene_)scene_->ambient_update(now);if(title_scene_)title_scene_->update(now);
        if(background_)return {script::RunState::yielded,0};if(release_input_){input={};release_input_=false;}
        options_view_->update({input.selection,input.cancel || input.toggle_window,input.pointer_held,input.page_delta});
        if(auto command=options_view_->take_command()){
            if(command->kind==OptionsCommand::Kind::close)back(now);
            else if(options_command_)options_command_(*command);
            else switch(command->kind){
            case OptionsCommand::Kind::reset:
                if(preferences().confirmation){if(!options_text_)throw script::Unsupported("Native options confirmation text loader is not prepared");options_view_->confirm_reset(preferences().skin,options_text_);break;}
                [[fallthrough]];
            case OptionsCommand::Kind::reset_confirmed:
                preferences(options_defaults(preferences()));if(scene_)scene_->options_reset_sound();else title_scene_->options_reset_sound();break;
            case OptionsCommand::Kind::preview_effects:if(scene_)scene_->options_preview_effects();else title_scene_->options_preview_effects();break;
            case OptionsCommand::Kind::preview_voice:if(scene_)scene_->options_preview_voice();else title_scene_->options_preview_voice();break;
            default:throw script::Unsupported("Unknown native options owner command");
            }
        }
        return {script::RunState::yielded,0};
    }
    if(title_scene_){
        if(release_input_){input={};release_input_=false;}
        TitleInput title_input;if(page()==Page::title){title_input.selection=input.selection;title_input.selection.confirm|=input.advance;title_input.cancel=input.cancel;}
        title_scene_->update(now,title_input);if(auto command=title_scene_->take_command())switch(command->kind){
        case TitleCommand::Kind::start:start({command->scene,0,false},now);break;
        case TitleCommand::Kind::load:open(Page::load,now);break;
        case TitleCommand::Kind::options:open(Page::options,now);break;
        case TitleCommand::Kind::extra:extra_=command->extra;open(Page::extras,now);break;
        case TitleCommand::Kind::quit:close(now);break;
        }
        return {script::RunState::yielded,0};
    }
    if(!scene_)return {script::RunState::yielded,0};
    if(background_ || page()!=Page::story){scene_->ambient_update(now);return {script::RunState::yielded,0};}
    if(release_input_){input={};release_input_=false;}
    if(input.toggle_window && preferences().right_click){
        if(preferences().right_click==2){if(!save_services_.images)throw script::Unsupported("Native right-click save-page services are not prepared");open(Page::save,now);return {script::RunState::yielded,0};}
        if(!options_loader_)throw script::Unsupported("Native right-click settings view is not prepared");
        open(Page::options,now);return {script::RunState::yielded,0};
    }
    const auto batch=scene_->advance_frame(now,input,clock,budget);if(scene_->title_requested())title(now);else if(batch.state==script::RunState::stopped)throw script::Unsupported("Native story ended without a title transition");return batch;
}
void Application::close(std::int64_t now){observe(now);if(scene_)scene_->flush_progress();progress_->flush();save_view_.reset();scene_.reset();title_scene_.reset();options_view_.reset();pages_.resize(1);pages_[0]=Page::closed;release_input_=true;}
}
