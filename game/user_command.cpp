#include "application.hpp"

namespace wa2::game {
UserMenuState Application::user_menu()const{
    UserMenuState state;if(page()==Page::closed || background_)return state;
    const bool story=page()==Page::story && scene_ && !scene_->movie_active();
    const bool title=page()==Page::title && title_scene_ && title_scene_->stage()==TitleStage::view && title_scene_->view().phase()==TitlePhase::menu;
    const auto enable=[&](UserCommand command,bool value){if(value)state.enabled|=command_bit(command);};
    enable(UserCommand::quit,true);enable(UserCommand::title,bool(scene_));enable(UserCommand::fullscreen,bool(display_));
    enable(UserCommand::settings,(story || title) && bool(options_loader_));
    enable(UserCommand::movie_high,true);enable(UserCommand::movie_low,true);
    enable(UserCommand::save,story && bool(save_services_.images));enable(UserCommand::load,(story || title) && bool(save_services_.images));
    enable(UserCommand::quick_save,story && bool(save_services_.images) && bool(save_services_.stamp) && bool(save_services_.thumbnail));
    enable(UserCommand::quick_load,story && bool(save_services_.images));
    enable(UserCommand::backlog,story && bool(backlog_loader_) && scene_->backlog().entries().size()>=2);
    for(auto command:{UserCommand::hide,UserCommand::automatic,UserCommand::skip})enable(command,story);
    if(preferences().fullscreen)state.checked|=command_bit(UserCommand::fullscreen);
    state.checked|=command_bit(preferences().movie_quality?UserCommand::movie_low:UserCommand::movie_high);
    if(scene_){if(!scene_->window_visible())state.checked|=command_bit(UserCommand::hide);if(scene_->modes().auto_mode)state.checked|=command_bit(UserCommand::automatic);if(scene_->modes().latched_skip)state.checked|=command_bit(UserCommand::skip);}
    return state;
}
bool Application::user_command(UserCommand command,std::int64_t now){
    observe(now);if(!valid(command))throw std::invalid_argument("Unknown native user command");
    if(!(user_menu().enabled&command_bit(command))){if(scene_)scene_->cue(audio::SystemCue::reject);else if(title_scene_)title_scene_->cue(audio::SystemCue::reject);return false;}
    try{switch(command){
    case UserCommand::settings:open(Page::options,now);break;
    case UserCommand::save:control(script::StoryControl::save,now);break;
    case UserCommand::load:open(Page::load,now);break;
    case UserCommand::quick_save:control(script::StoryControl::quick_save,now);break;
    case UserCommand::quick_load:control(script::StoryControl::quick_load,now);break;
    case UserCommand::backlog:control(script::StoryControl::backlog,now);break;
    case UserCommand::hide:control(script::StoryControl::hide,now);break;
    case UserCommand::automatic:control(script::StoryControl::automatic,now);break;
    case UserCommand::skip:control(script::StoryControl::skip,now);break;
    case UserCommand::fullscreen:{auto next=preferences();next.fullscreen=!next.fullscreen;preferences(next);break;}
    case UserCommand::movie_high:case UserCommand::movie_low:{auto next=preferences();next.movie_quality=command==UserCommand::movie_low?1:0;preferences(next);break;}
    case UserCommand::title:title(now);break;
    case UserCommand::quit:close(now);break;
    default:throw std::invalid_argument("Unknown native user command");
    }}catch(const std::exception& error){if(error_)report_error(error.what(),now);else throw;return false;}return true;
}
}
