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
    next->suspend(background_,background_,now);if(scene_)scene_->flush_progress();close_backlog();save_view_.reset();music_view_.reset();extra_grid_.reset();commentary_view_.reset();seed_=next->random_state();scene_=std::move(next);title_scene_.reset();options_view_.reset();extra_=-1;pages_.resize(1);pages_[0]=Page::story;release_input_=true;++scene_revision_;
}
bool Application::preferences(const Preferences& next){
    require_open();next.validate();const auto state=options_view_?std::optional(options_state(next)):std::nullopt;const auto previous=preferences();const bool display_changed=display_ && next.fullscreen!=previous.fullscreen;
    if(display_changed)display_(next.fullscreen);
    try{if(!preferences_.replace(next))return false;}catch(const std::exception& original){
        if(display_changed)try{display_(previous.fullscreen);}catch(const std::exception& rollback){throw std::runtime_error(std::string(original.what())+"; native display rollback failed: "+rollback.what());}throw;
    }
    if(scene_)scene_->preferences(preferences_.current());if(title_scene_)title_scene_->preferences(preferences_.current());if(state){options_view_->state(*state);options_view_->timing(options_timing(next));}return true;
}
void Application::prepare_display(std::function<void(bool)> display){require_open();if(!display)throw std::invalid_argument("Missing native display adapter");display(preferences().fullscreen);display_=std::move(display);}
void Application::prepare_errors(std::function<void(std::string_view)> error){require_open();if(!error)throw std::invalid_argument("Missing native operation error adapter");error_=std::move(error);}
void Application::report_error(std::string_view diagnostic,std::int64_t now){
    if(clock_revision_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Native recovery clock revision exhausted");
    if(scene_)scene_->suspend(true,background_,now);recover_clock_=release_input_=true;++clock_revision_;error_(diagnostic);
}
void Application::prepare_options(OptionsView::Loader loader,std::function<void(OptionsCommand)> commands,ConfirmationView::Text text){
    require_open();if(!loader)throw std::invalid_argument("Missing native options image loader");if(options_view_)throw std::logic_error("Cannot replace native options services while their view is open");options_loader_=std::move(loader);options_command_=std::move(commands);options_text_=std::move(text);
}
void Application::prepare_saves(SaveServices services){
    require_open();if(!services.images || !services.text || !services.stamp)throw std::invalid_argument("Missing native save menu services");
    if(save_view_)throw std::logic_error("Cannot replace native save services while their view is open");
    if(!services.thumbnail)services.thumbnail=std::move(save_services_.thumbnail);save_services_=std::move(services);
}
void Application::prepare_backlog(BacklogView::Loader images,BacklogView::Text text){
    require_open();if(!images || !text)throw std::invalid_argument("Missing native backlog menu services");if(backlog_view_)throw std::logic_error("Cannot replace native backlog services while its view is open");backlog_loader_=std::move(images);backlog_text_=std::move(text);
}
void Application::prepare_extras(MusicView::Loader images){require_open();if(!images)throw std::invalid_argument("Missing native extras image loader");if(page()==Page::extras)throw std::logic_error("Cannot replace native extras services while open");extras_loader_=std::move(images);}
void Application::close_backlog(){if(replay_entry_ && scene_)scene_->stop_replay();replay_.clear();replay_next_=0;replay_entry_.reset();backlog_view_.reset();}
void Application::control(script::StoryControl action,std::int64_t now){
    if(!scene_ || page()!=Page::story)throw std::logic_error("Native story control requires its story page");
    switch(action){
    case script::StoryControl::quick_save:{if(!save_services_.images)throw script::Unsupported("Native quick-save services are not prepared");const auto document=save_document();saves_.write(quick_slot,document);break;}
    case script::StoryControl::quick_load:load(quick_slot,now);break;
    case script::StoryControl::options:if(!options_loader_)throw script::Unsupported("Native story settings services are not prepared");open(Page::options,now);break;
    case script::StoryControl::save:if(!save_services_.images)throw script::Unsupported("Native story save services are not prepared");open(Page::save,now);break;
    case script::StoryControl::load:if(!save_services_.images)throw script::Unsupported("Native story load services are not prepared");open(Page::load,now);break;
    case script::StoryControl::backlog:if(!backlog_loader_)throw script::Unsupported("Native story backlog services are not prepared");if(scene_->backlog().entries().size()<2){scene_->cue(audio::SystemCue::reject);return;}open(Page::backlog,now);break;
    case script::StoryControl::skip:case script::StoryControl::automatic:case script::StoryControl::hide:scene_->story_control(action);break;
    default:throw script::Error("Unknown native story control");
    }
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
    observe(now);if(start.scene.empty() || start.scene.size()>240 || start.label<0 || start.replay<0 || start.replay>24)throw std::invalid_argument("Invalid native story entry");if(scene_revision_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Native scene revision exhausted");
    // Prepare completely before releasing the active owner or navigation.
    const auto seed=scene_?scene_->random_state():title_scene_?title_scene_->random_state():seed_;auto next=std::make_unique<Scene>(opener_,seed,progress_,services_);next->preferences(preferences_.current());next->replay_mode(start.replay);next->start(start.scene,start.label);
    std::optional<ReplayReturn> returning;
    if(start.replay && page()==Page::extras && extra_grid_ && extra_grid_->kind()==ExtraGridKind::replay){title_scene_->pause(true,now);returning=ReplayReturn{std::move(title_scene_),std::move(extra_grid_),extra_};}
    replace_scene(std::move(next),now);replay_return_=std::move(returning);
}
void Application::open(Page page,std::int64_t now){
    observe(now);if(page!=Page::options && page!=Page::save && page!=Page::load && page!=Page::backlog && page!=Page::extras)throw std::invalid_argument("Invalid native menu destination");
    if((page==Page::save || page==Page::backlog) && (!scene_ || pages_.front()!=Page::story))throw std::logic_error("This native menu requires an active story");
    if(page==Page::extras && pages_.front()!=Page::title)throw std::logic_error("Native extras require the title page");
    if(scene_ && scene_->movie_active())throw script::Unsupported("Native modal menu during movie playback is not implemented");
    if(page==this->page())return;if(pages_.size()>=8)throw std::logic_error("Native menu nesting exceeds its limit");
    std::unique_ptr<OptionsView> next;std::unique_ptr<SaveView> next_save;std::unique_ptr<BacklogView> next_backlog;
    if(page==Page::options && options_loader_){
        if(options_view_ || (this->page()!=Page::story && this->page()!=Page::title))throw script::Unsupported("Native options over another modal page are not implemented");
        if(!scene_ && (!title_scene_ || title_scene_->stage()!=TitleStage::view))throw script::Unsupported("Native options require a prepared title view or story");
        next=std::make_unique<OptionsView>(options_loader_,[this](const OptionsState& state){const auto next=options_preferences(state,preferences());
            if(next.fullscreen!=preferences().fullscreen && !display_)throw script::Unsupported("Native options display-mode adapter is not prepared");
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
    if(page==Page::backlog && backlog_loader_){
        if(this->page()!=Page::story || backlog_view_)throw script::Unsupported("Native backlog over another modal page is not implemented");
        next_backlog=std::make_unique<BacklogView>(backlog_loader_,backlog_text_,[this](audio::SystemCue cue){scene_->cue(cue);});
        next_backlog->prepare({preferences().skin,scene_->dialogue_state().novel().enabled(),std::uint32_t(scene_->dialogue_state().page())},scene_->backlog());
    }
    pages_.push_back(page);release_input_=true;suspend_scene(now);
    if(next)options_view_=std::move(next);
    if(next_save)save_view_=std::move(next_save);
    if(next_backlog)backlog_view_=std::move(next_backlog);
}
void Application::open_extra(std::int32_t extra,std::int64_t now){
    observe(now);if(page()!=Page::title || !title_scene_ || title_scene_->stage()!=TitleStage::view || !title_setup_)throw std::logic_error("Native extras require a prepared title page");
    if(!extras_loader_)throw script::Unsupported("Native extras image services are not prepared");if(extra<1 || extra>4)throw script::Unsupported("Native extra view is not implemented: "+std::to_string(extra));
    std::unique_ptr<MusicView> music;std::unique_ptr<ExtraGrid> grid;std::unique_ptr<CommentaryView> commentary;
    if(extra==2){music=std::make_unique<MusicView>(extras_loader_,[this](auto cue){title_scene_->cue(cue);});music->prepare(title_setup_->content,progress_->state());
        if(progress_->state().flag(104) && progress_->state().flag(163)){progress_->state().set_flag(134,1);progress_->flush();}title_scene_->music_preview({},120);
    }else if(extra==4){commentary=std::make_unique<CommentaryView>(extras_loader_,[this](auto cue){title_scene_->cue(cue);});commentary->prepare();title_scene_->music_preview({},120);
    }else{grid=std::make_unique<ExtraGrid>(extras_loader_,[this](auto cue){title_scene_->cue(cue);});grid->prepare(extra==1?ExtraGridKind::gallery:ExtraGridKind::replay,title_setup_->content,progress_->state());title_scene_->music_preview(extra==1?41:15,extra==1?30:0);}
    open(Page::extras,now);extra_=extra;music_view_=std::move(music);extra_grid_=std::move(grid);commentary_view_=std::move(commentary);
}
bool Application::back(std::int64_t now){observe(now);if(pages_.size()==1)return false;const auto leaving=page();if(leaving==Page::extras && (music_view_ || extra_grid_ || commentary_view_)){title_scene_->commentary_stop();title_scene_->music_preview({},120);music_view_.reset();extra_grid_.reset();commentary_view_.reset();extra_=-1;}pages_.pop_back();if(leaving==Page::options)options_view_.reset();if(leaving==Page::save || leaving==Page::load)save_view_.reset();if(leaving==Page::backlog)close_backlog();if(page()==Page::title && title_scene_ && title_scene_->view().modal_parent())title_scene_->resume();release_input_=true;suspend_scene(now);return true;}
void Application::return_replay(std::int64_t now){
    if(!scene_ || !scene_->replay_mode() || !replay_return_)throw std::logic_error("Native replay has no retained extra owner");if(scene_revision_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Native scene revision exhausted");
    auto& returning=*replay_return_;returning.title->preferences(preferences());returning.title->music_preview(15,0);scene_->flush_progress();returning.title->pause(background_,now);
    close_backlog();save_view_.reset();options_view_.reset();scene_.reset();title_scene_=std::move(returning.title);extra_grid_=std::move(returning.grid);extra_=returning.extra;replay_return_.reset();
    pages_={Page::title,Page::extras};release_input_=true;++scene_revision_;
}
void Application::title(std::int64_t now){
    if(scene_ && scene_->replay_mode() && !replay_return_){
        observe(now);if(!title_setup_ || !extras_loader_)throw script::Unsupported("Native replay exit needs prepared title/extra services");const auto seed=scene_->random_state();auto startup=title_setup_->startup;startup.movies=false;
        auto title=std::make_unique<TitleScene>(opener_,progress_,preferences(),title_setup_->content,startup,seed,title_setup_->services);title->restore_extras_parent();title->pause(true,now);
        auto grid=std::make_unique<ExtraGrid>(extras_loader_,[this](auto cue){title_scene_->cue(cue);});grid->prepare(ExtraGridKind::replay,title_setup_->content,progress_->state(),unsigned(scene_->replay_mode()-1)/12);
        replay_return_=ReplayReturn{std::move(title),std::move(grid),3};return_replay(now);return;
    }
    if(scene_ && scene_->replay_mode() && replay_return_){observe(now);return_replay(now);return;}
    observe(now);const auto seed=scene_?scene_->random_state():title_scene_?title_scene_->random_state():seed_;std::unique_ptr<TitleScene> next;
    if(title_setup_){auto startup=title_setup_->startup;startup.movies=false;next=std::make_unique<TitleScene>(opener_,progress_,preferences_.current(),title_setup_->content,startup,seed,title_setup_->services);next->pause(background_,now);}
    if(scene_)scene_->flush_progress();close_backlog();save_view_.reset();music_view_.reset();extra_grid_.reset();commentary_view_.reset();replay_return_.reset();scene_.reset();title_scene_=std::move(next);options_view_.reset();seed_=seed;extra_=-1;pages_.resize(1);pages_[0]=Page::title;release_input_=true;
}
void Application::background(bool value,std::int64_t now){observe(now);if(background_==value)return;background_=value;release_input_=true;suspend_scene(now);if(value)progress_->flush();}
void Application::save(unsigned slot){require_open();if(!scene_)throw std::logic_error("Native save requires an active story");saves_.write(slot,scene_->checkpoint());}
void Application::load(unsigned slot,std::int64_t now){
    observe(now);auto bytes=saves_.read(slot);if(!bytes)throw std::runtime_error("Native save slot is empty");auto next=Scene::restore(opener_,progress_,*bytes,now,resolver_,preferences_.current(),services_);const bool replay=next->replay_mode()!=0;replace_scene(std::move(next),now);if(!replay)replay_return_.reset();
}
FrameBatch Application::frame(std::int64_t now,Input input,const std::function<std::int64_t()>& clock,FrameBudget budget){
    observe(now);
    if(recover_clock_){suspend_scene(now);recover_clock_=false;input={};}
    if(!input.commands.empty()){
        if(input.commands.size()>maximum_user_commands)throw std::invalid_argument("Too many native user commands");for(auto command:input.commands)if(!valid(command))throw std::invalid_argument("Unknown native user command");
        for(auto command:input.commands){const auto revision=clock_revision_;user_command(command,now);if(page()==Page::closed || clock_revision_!=revision)break;}return {script::RunState::yielded,0};
    }
    if(input.cancel && pages_.size()>1 && !(page()==Page::options && options_view_) && !save_view_ && !backlog_view_ && !music_view_ && !extra_grid_ && !commentary_view_){back(now);input={};}
    if(page()==Page::extras && commentary_view_){
        try{title_scene_->update(now);if(background_)return {script::RunState::yielded,0};if(release_input_){input={};release_input_=false;}
            commentary_view_->update({input.selection,input.cancel || input.toggle_window},title_scene_->commentary_playing());if(auto command=commentary_view_->take_command()){
                if(command->kind==CommentaryCommand::Kind::close)back(now);else if(command->kind==CommentaryCommand::Kind::stop)title_scene_->commentary_stop();else title_scene_->commentary_play(command->speaker);}}
        catch(const std::exception& error){if(title_scene_)title_scene_->commentary_stop();if(commentary_view_){commentary_view_->failed();if(commentary_view_->phase()==CommentaryPhase::idle)back(now);}if(error_)report_error(error.what(),now);else throw;}
        return {script::RunState::yielded,0};
    }
    if(page()==Page::extras && extra_grid_){
        title_scene_->update(now);if(background_)return {script::RunState::yielded,0};if(release_input_){input={};release_input_=false;}
        try{extra_grid_->update({input.selection,input.cancel || input.toggle_window,input.page_delta});if(auto command=extra_grid_->take_command()){
                if(command->kind==ExtraGridCommand::Kind::close)back(now);else start({"9999",0,std::int32_t(command->entry)},now);}}
        catch(const std::exception& error){if(extra_grid_ && extra_grid_->phase()==ExtraGridPhase::idle)back(now);if(error_)report_error(error.what(),now);else throw;}
        return {script::RunState::yielded,0};
    }
    if(page()==Page::extras && music_view_){
        title_scene_->update(now);if(background_)return {script::RunState::yielded,0};if(release_input_){input={};release_input_=false;}
        music_view_->update({input.selection,input.cancel || input.toggle_window,input.page_delta});if(auto command=music_view_->take_command()){
            if(command->kind==MusicCommand::Kind::close)back(now);else try{title_scene_->music_preview(command->kind==MusicCommand::Kind::play?std::optional(command->song):std::nullopt,command->fade);music_view_->played(command->kind==MusicCommand::Kind::play?std::optional(command->index):std::nullopt);}
            catch(const std::exception& error){if(error_)report_error(error.what(),now);else throw;}}
        return {script::RunState::yielded,0};
    }
    if(page()==Page::backlog && backlog_view_){
        scene_->ambient_update(now);if(background_)return {script::RunState::yielded,0};if(release_input_){input={};release_input_=false;}
        if(replay_entry_ && !scene_->replay_playing()){
            if(replay_next_<replay_.size())scene_->replay_voice(replay_[replay_next_++]);else{replay_entry_.reset();replay_.clear();}
        }
        backlog_view_->playing(replay_entry_);backlog_view_->update({input.selection,input.cancel || input.toggle_window,input.pointer_held,input.page_delta,input.scroll});
        if(auto command=backlog_view_->take_command()){
            if(command->kind==BacklogCommand::Kind::close)back(now);
            else{auto lines=backlog_view_->entries().at(command->entry).voices;if(lines.empty())throw script::Error("Native backlog replay has no recorded voice");
                scene_->stop_replay();scene_->replay_voice(lines.front());replay_=std::move(lines);replay_next_=1;replay_entry_=command->entry;}
        }
        return {script::RunState::yielded,0};
    }
    if((page()==Page::save || page()==Page::load) && save_view_){
        if(scene_)scene_->ambient_update(now);if(title_scene_)title_scene_->update(now);
        if(background_)return {script::RunState::yielded,0};if(release_input_){input={};release_input_=false;}
        save_view_->update({input.selection,input.cancel || input.toggle_window,input.page_delta});
        if(auto command=save_view_->take_command()){
            if(command->kind==SaveCommand::Kind::close)back(now);
            else try{
                if(command->kind==SaveCommand::Kind::load)load(command->slot,now);
                else{auto document=save_document();saves_.write(command->slot,document);save_view_->succeeded(std::move(document.preview));}
            }catch(const std::exception& error){if(save_view_)save_view_->failed(error.what());if(error_)report_error(error.what(),now);else throw;}
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
        case TitleCommand::Kind::extra:open_extra(command->extra,now);break;
        case TitleCommand::Kind::quit:close(now);break;
        }
        return {script::RunState::yielded,0};
    }
    if(!scene_)return {script::RunState::yielded,0};
    if(background_ || page()!=Page::story){scene_->ambient_update(now);return {script::RunState::yielded,0};}
    if(release_input_){input={};release_input_=false;}
    if(input.selection.pointer && input.selection.activate)if(auto action=scene_->control_at(input.selection.x,input.selection.y)){
        try{control(*action,now);}catch(const std::exception& error){if(error_)report_error(error.what(),now);else throw;}return {script::RunState::yielded,0};
    }
    if(input.scroll>0 && backlog_loader_ && scene_->backlog().entries().size()>=2){open(Page::backlog,now);return {script::RunState::yielded,0};}
    if(input.toggle_window && preferences().right_click){
        if(preferences().right_click==2){if(!save_services_.images)throw script::Unsupported("Native right-click save-page services are not prepared");open(Page::save,now);return {script::RunState::yielded,0};}
        if(!options_loader_)throw script::Unsupported("Native right-click settings view is not prepared");
        open(Page::options,now);return {script::RunState::yielded,0};
    }
    const auto batch=scene_->advance_frame(now,input,clock,budget);if(scene_->title_requested())title(now);else if(batch.state==script::RunState::stopped)throw script::Unsupported("Native story ended without a title transition");return batch;
}
void Application::close(std::int64_t now){observe(now);if(scene_)scene_->flush_progress();progress_->flush();close_backlog();save_view_.reset();music_view_.reset();extra_grid_.reset();commentary_view_.reset();replay_return_.reset();scene_.reset();title_scene_.reset();options_view_.reset();pages_.resize(1);pages_[0]=Page::closed;release_input_=true;}
}
