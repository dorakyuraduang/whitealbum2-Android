#include "application.hpp"
#include <limits>

namespace wa2::game {
Application::Application(resource::BankOpener opener,std::shared_ptr<progress::Store> progress,std::shared_ptr<PreferenceStorage> preferences,std::shared_ptr<SaveStorage> saves,Scene::SavedImageResolver resolve,std::uint32_t seed)
    :opener_(std::move(opener)),progress_(std::move(progress)),preferences_(std::move(preferences)),saves_(std::move(saves)),resolver_(std::move(resolve)),seed_(seed){
    if(!opener_ || !progress_ || !resolver_)throw std::invalid_argument("Missing native application resources, progress or image resolver");
}
void Application::require_open()const{if(page()==Page::closed)throw std::logic_error("Native application is closed");}
void Application::observe(std::int64_t now){require_open();if(now<0 || (clock_started_ && now<observed_))throw std::invalid_argument("Native application clock moved backwards");observed_=now;clock_started_=true;}
void Application::suspend_scene(std::int64_t now){if(scene_)scene_->suspend(background_ || page()!=Page::story,background_,now);}
void Application::replace_scene(std::unique_ptr<Scene> next,std::int64_t now){
    if(scene_revision_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Native scene revision exhausted");
    next->suspend(background_,background_,now);if(scene_)scene_->flush_progress();scene_=std::move(next);pages_.resize(1);pages_[0]=Page::story;release_input_=true;++scene_revision_;
}
bool Application::preferences(const Preferences& next){require_open();if(!preferences_.replace(next))return false;if(scene_)scene_->preferences(preferences_.current());return true;}
void Application::start(const StoryStart& start,std::int64_t now){
    observe(now);if(start.scene.empty() || start.scene.size()>240 || start.label<0)throw std::invalid_argument("Invalid native story entry");
    // Prepare completely before releasing the active owner or navigation.
    auto next=std::make_unique<Scene>(opener_,seed_,progress_);next->preferences(preferences_.current());next->replay_mode(start.replay);next->start(start.scene,start.label);replace_scene(std::move(next),now);++seed_;
}
void Application::open(Page page,std::int64_t now){
    observe(now);if(page!=Page::options && page!=Page::save && page!=Page::load && page!=Page::backlog && page!=Page::extras)throw std::invalid_argument("Invalid native menu destination");
    if((page==Page::save || page==Page::backlog) && (!scene_ || pages_.front()!=Page::story))throw std::logic_error("This native menu requires an active story");
    if(page==Page::extras && pages_.front()!=Page::title)throw std::logic_error("Native extras require the title page");
    if(scene_ && scene_->movie_active())throw script::Unsupported("Native modal menu during movie playback is not implemented");
    if(page==this->page())return;if(pages_.size()>=8)throw std::logic_error("Native menu nesting exceeds its limit");
    pages_.push_back(page);release_input_=true;suspend_scene(now);
}
bool Application::back(std::int64_t now){observe(now);if(pages_.size()==1)return false;pages_.pop_back();release_input_=true;suspend_scene(now);return true;}
void Application::title(std::int64_t now){observe(now);if(scene_)scene_->flush_progress();scene_.reset();pages_.resize(1);pages_[0]=Page::title;release_input_=true;}
void Application::background(bool value,std::int64_t now){observe(now);if(background_==value)return;background_=value;release_input_=true;suspend_scene(now);if(value)progress_->flush();}
void Application::save(unsigned slot){require_open();if(!scene_)throw std::logic_error("Native save requires an active story");saves_.write(slot,scene_->checkpoint());}
void Application::load(unsigned slot,std::int64_t now){
    observe(now);auto bytes=saves_.read(slot);if(!bytes)throw std::runtime_error("Native save slot is empty");auto next=Scene::restore(opener_,progress_,*bytes,now,resolver_,preferences_.current());replace_scene(std::move(next),now);
}
FrameBatch Application::frame(std::int64_t now,Input input,const std::function<std::int64_t()>& clock,FrameBudget budget){
    observe(now);if(!scene_)return {script::RunState::yielded,0};
    if(background_ || page()!=Page::story){scene_->ambient_update(now);return {script::RunState::yielded,0};}
    if(release_input_){input={};release_input_=false;}
    const auto batch=scene_->advance_frame(now,input,clock,budget);if(scene_->title_requested())title(now);else if(batch.state==script::RunState::stopped)throw script::Unsupported("Native story ended without a title transition");return batch;
}
void Application::close(std::int64_t now){observe(now);if(scene_)scene_->flush_progress();progress_->flush();scene_.reset();pages_.resize(1);pages_[0]=Page::closed;release_input_=true;}
}
