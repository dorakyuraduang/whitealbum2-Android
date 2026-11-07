#include "title_scene.hpp"
#include "audio_resources.hpp"
#include "options_audio.hpp"
#include <algorithm>

namespace wa2::game {
TitleContent installed_title_content(const resource::BankOpener& opener){
    if(!opener)throw std::invalid_argument("Missing native installation resource opener");
    return {bool(opener("IC/bak.pak")),false,true};
}
TitleScene::TitleScene(resource::BankOpener opener,std::shared_ptr<progress::Store> progress,const Preferences& settings,TitleContent content,TitleStartup startup,std::uint32_t seed,TitleServices services)
    :resources_(std::move(opener)),mixer_(std::make_shared<audio::Mixer>()),progress_(std::move(progress)),
    sounds_(*mixer_,[this](auto id){return audio::effect_resource(resources_,id);}),
    music_([this](auto id,bool loop){return services_.music?services_.music(id,loop):audio::play_ogg(*mixer_,audio::music_resources(resources_,id,loop));},[this](auto id){progress_->state().set_flag(std::uint32_t(100+id),1);progress_->flush();}),
    effects_([this](auto id,bool loop){if(services_.effects)return services_.effects(id,loop);auto resource=audio::effect_resource(resources_,id);return resource.ogg?audio::play_ogg(*mixer_,{resource.source,loop?resource.source:nullptr}):audio::play_wave(*mixer_,resource.source,loop);}),
    voices_([this](const auto& line){if(services_.speech)return services_.speech(line);auto source=audio::speech_resource(resources_,line.chapter,line.line,line.speaker);return audio::play_ogg(*mixer_,{source,line.loop?source:nullptr});},[]{return -1;},[]{return -1;}),
    random_(seed),view_([this](auto name){return resources_.image("grp",name);},[this]{return random_.next();},[this](auto cue){sounds_.play(cue);},[this](auto song,auto fade){if(song)music_.play(*song,fade,1,255);else music_.stop(fade);}),
    services_(std::move(services)),content_(content),startup_(startup),stage_(startup.movies?TitleStage::company_movie:TitleStage::view){
    if(!progress_)throw std::invalid_argument("Missing native title progress storage");if(startup_.movie_suffix && *startup_.movie_suffix>2)throw std::invalid_argument("Invalid native title movie suffix");
    preferences(settings);sounds_.prepare();if(startup_.movies)begin_movie("mv00");else begin_view();
}
TitleScene::~TitleScene(){commentary_stop();if(movie_)movie_->stop();}
void TitleScene::preferences(const Preferences& value){
    value.validate();preferences_=value;music_.settings(value.effective_master(),value.effective_music());sounds_.settings(value.effective_master(),value.effective_effects());
    effects_.settings(value.effective_master(),value.effective_effects());voices_.settings(value.effective_master(),value.speech,value.mute_adult);voices_.enabled(value.voice_enabled);voices_.character_switches(value.character_voice);
    if(movie_)movie_->gain(value.movie_amplitude());
    if(commentary_)commentary_->gain(std::uint8_t(std::clamp(std::int64_t(255)*value.effective_master()*value.speech/65536,std::int64_t(0),std::int64_t(255))));
}
void TitleScene::options_preview_effects(){game::options_preview_effects(effects_,preferences_);}
void TitleScene::options_preview_voice(){game::options_preview_voice(voices_,random_,preferences_);}
void TitleScene::options_reset_sound(){game::options_reset_sound(effects_);}
void TitleScene::resume(){if(!view_.modal_parent())throw script::Error("Native title has no modal parent");music_.play(31,0,1,255);view_.resume(progress_->state());}
void TitleScene::music_preview(std::optional<std::int32_t> song,std::int32_t fade){
    if(stage_!=TitleStage::view || fade<0 || fade>120 || (song && std::uint32_t(*song)>=200))throw std::invalid_argument("Invalid native title music preview");
    if(song)music_.play(*song,fade,1,255);else music_.stop(fade);
}
void TitleScene::commentary_play(unsigned speaker){
    if(stage_!=TitleStage::view || speaker<1 || speaker>5)throw std::invalid_argument("Invalid native commentary speaker");
    const script::SpokenLine line{int(speaker),9500,int(speaker)-1,false};auto next=services_.speech?services_.speech(line):audio::play_ogg(*mixer_,{audio::speech_resource(resources_,line.chapter,line.line,line.speaker),{}});
    if(!next)throw script::Error("Native commentary factory returned no voice");next->gain(std::uint8_t(std::clamp(std::int64_t(255)*preferences_.effective_master()*preferences_.speech/65536,std::int64_t(0),std::int64_t(255))));commentary_stop();commentary_=std::move(next);
}
bool TitleScene::commentary_playing()const{if(!commentary_)return false;if(const auto error=commentary_->failure();!error.empty())throw script::Error("Native commentary playback failed: "+error);return !commentary_->finished();}
text::PreparedText TitleScene::menu_text(std::string_view raw,const text::TextStyle& style){if(!menu_fonts_)menu_fonts_=std::make_unique<text::FontBook>(resources_.required("fon"));return menu_fonts_->prepare(text::layout(raw,style));}
void TitleScene::begin_movie(std::string_view stem){
    const auto name=std::string(stem)+char('0'+startup_.movie_suffix.value_or(unsigned(preferences_.movie_quality)))+".pak";
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
    music_.update();effects_.update();voices_.update();
}
}
