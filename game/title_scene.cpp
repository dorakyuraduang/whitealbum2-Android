#include "title_scene.hpp"
#include "audio_resources.hpp"
#include <algorithm>

namespace wa2::game {
TitleScene::TitleScene(resource::BankOpener opener,std::shared_ptr<progress::Store> progress,const Preferences& settings,TitleContent content,TitleStartup startup,std::uint32_t seed,TitleServices services)
    :resources_(std::move(opener)),mixer_(std::make_shared<audio::Mixer>()),progress_(std::move(progress)),
    sounds_(*mixer_,[this](auto id){return audio::effect_resource(resources_,id);}),
    music_([this](auto id,bool loop){return services_.music?services_.music(id,loop):audio::play_ogg(*mixer_,audio::music_resources(resources_,id,loop));},[this](auto id){progress_->state().set_flag(std::uint32_t(100+id),1);progress_->flush();}),
    random_(seed),view_([this](auto name){return resources_.image("grp",name);},[this]{return random_.next();},[this](auto cue){sounds_.play(cue);},[this](auto song,auto fade){if(song)music_.play(*song,fade,1,255);else music_.stop(fade);}),
    services_(std::move(services)),content_(content),startup_(startup),stage_(startup.movies?TitleStage::company_movie:TitleStage::view){
    if(!progress_)throw std::invalid_argument("Missing native title progress storage");if(startup_.movie_suffix>2)throw std::invalid_argument("Invalid native title movie suffix");
    preferences(settings);sounds_.prepare();if(startup_.movies)begin_movie("mv00");else begin_view();
}
TitleScene::~TitleScene(){if(movie_)movie_->stop();}
void TitleScene::preferences(const Preferences& value){
    value.validate();preferences_=value;music_.settings(value.master,value.music);sounds_.settings(value.master,value.effects);
    if(movie_)movie_->gain(value.movie_amplitude());
}
void TitleScene::begin_movie(std::string_view stem){
    const auto name=std::string(stem)+char('0'+startup_.movie_suffix)+".pak";
    auto next=services_.movie?services_.movie(name):std::make_unique<movie::Player>(resources_.movie(name),mixer_);
    if(!next)throw script::Error("Native title movie factory returned no player");next->gain(preferences_.movie_amplitude());movie_=std::move(next);
}
void TitleScene::begin_view(){view_.prepare(content_,progress_->state());stage_=TitleStage::view;}
void TitleScene::pause(bool value,std::int64_t now){timeline_.pause(value,now);if(value)mixer_->pause(true);if(movie_)movie_->pause(value,now);if(!value)mixer_->pause(false);}
void TitleScene::update(std::int64_t now,TitleInput input){
    timeline_.sample(now);if(timeline_.paused())return;
    switch(stage_){
    case TitleStage::company_movie:case TitleStage::opening_movie:{
        if(!movie_)throw script::Error("Native title lost its startup movie");movie_->update(now);if(input.selection.activate || input.selection.confirm)movie_->stop();if(!movie_->finished())break;
        movie_.reset();if(stage_==TitleStage::opening_movie){begin_view();break;}
        const auto& state=progress_->state();const bool opening=state.flag(210)!=0 || (content_.local_introductory && state.flag(202)!=0);
        if(opening){stage_=TitleStage::opening_delay;delay_=0;}else begin_view();break;
    }
    case TitleStage::opening_delay:
        if(delay_>2){const auto& state=progress_->state();
            if(content_.local_introductory && !state.flag(210)){progress_->state().set_flag(104,1);progress_->flush();begin_movie("mv02");}
            else begin_movie(state.flag(220)?"mv20":"mv10");stage_=TitleStage::opening_movie;
        }else ++delay_;break;
    case TitleStage::view:view_.update(input);break;
    }
    music_.update();
}
}
