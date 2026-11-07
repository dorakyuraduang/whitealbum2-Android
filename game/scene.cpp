#include "scene.hpp"
#include "audio_resources.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <charconv>
#include <sstream>
#include <iomanip>

namespace wa2::game {
Scene::Scene(resource::BankOpener opener,std::uint32_t seed,std::shared_ptr<progress::Store> progress)
    :resources_(std::move(opener)),mixer_(std::make_shared<audio::Mixer>()),system_sounds_(*mixer_,[this](auto id){return sound_resource(id);}),fonts_(resources_.required("fon")),
    library_(resources_.required("ck-gal")),session_(library_),random_(seed),progress_store_(std::move(progress)),reading_(progress_store_),builtins_(random_),
    controls_([this]{return milliseconds_;},[this]{return input_.advance || skipping();},[this]{return input_.advance || skipping();},[this]{return skipping();},[this]{return reading_.status(replay_mode_);}),
    progress_(progress_store_),
    choices_(progress_store_,[this]()->std::string_view{return session_.active()?session_.active()->program().name():statistics_.scene;}),
    selection_([this](auto name){return resources_.image("grp",name);},[this](auto raw){text::TextStyle style;style.rows=1;style.columns=34;style.line_gap=0;style.shadow={33,46,56};return fonts_.prepare(text::layout(raw,style));},
        [this](auto sound){system_sounds_.play(sound==script::SelectionSound::focus?audio::SystemCue::focus:sound==script::SelectionSound::accept?audio::SystemCue::accept:audio::SystemCue::reject);}),
    calendar_([this](auto name){return resources_.image("grp",name);},[this]{for(const auto& variable:library_.globals().variables())if(variable.name=="FLG_00")return variable.value.integer();throw script::Error("Missing calendar theme global FLG_00");}),
    music_([this](auto id,bool loop){return music(id,loop);},[this](auto id){
        if(!progress_store_)throw script::Error("Missing native music progress storage");progress_store_->state().set_flag(std::uint32_t(100+id),1);progress_store_->flush();}),
    effects_([this](auto id,bool loop){return sound(id,loop);},[this]{return skipping();},[this](auto id,bool loop,bool paused){return sound(id,loop,paused);},
        {[this](auto slot,auto id,const auto& voice){if(id>=0 && id<=65535)captions_.queue(std::uint32_t(id),subtitles::Kind::effect,slot,voice);},[this](auto slot){captions_.stop(subtitles::Kind::effect,slot);}}),
    voices_([this](const auto& line){return speech(line);},[this]{
        if(controls_.modes().voice_chapter!=-1)return controls_.modes().voice_chapter;
        std::int32_t chapter{};const auto& name=session_.active()?session_.active()->program().name():statistics_.scene;const auto value=std::from_chars(name.data(),name.data()+name.size(),chapter);
        if(value.ec!=std::errc{} || value.ptr!=name.data()+name.size())throw script::Error("scene has no numeric voice chapter");return chapter;
    },[this]{return dialogue_.speaker();},[this]{return controls_.modes().voice_offset;},[this]{return skipping();},[this]{return input_.advance;},
        {[this](auto slot,const auto& line,const auto& voice){captions_.queue((std::uint32_t(line.chapter)<<16)|std::uint32_t(line.line),subtitles::Kind::voice,slot,voice);},[this](auto slot){captions_.stop(subtitles::Kind::voice,slot);},
            [this](auto,const auto& line,auto volume){backlog_.note_voice({line.speaker,line.chapter,line.line,volume});}}),
    weather_([this](auto name){return resources_.image("grp",name);},[this]{return random_.next();},[this](auto name){return image::Animation::parse(resources_.data("grp",name));}),
    shake_([this]{return random_.next();}),
    backgrounds_([this](auto name){return resources_.image("bak",name,controls_.modes().background_chroma,controls_.modes().background_effect);},[this](bool keep_characters){bitmaps_.clear_retired();characters_.clear_retired();background_changing_=true;preserve_characters_=keep_characters;bitmaps_.retire_background();if(!keep_characters)characters_.retire_background();},
        [this](auto name){return resources_.image("grp",name);},[this](auto name){return resources_.image("grp",name,controls_.modes().background_chroma,controls_.modes().background_effect);}),
    bitmaps_([this](auto name,auto group,auto){return resources_.image(group,name);},[this](auto name,auto group){return image::Animation::parse(resources_.data(group,name));}),
    characters_([this](auto name){return resources_.image("char",name,controls_.modes().character_chroma,controls_.modes().character_effect);}),
    dialogue_(fonts_,[this](auto name){return resources_.image("grp",name);},[this](auto line){
        reading_.seen(session_.active()?session_.active()->program().name():statistics_.scene,line);controls_.reset_message_timer();++statistics_.messages;},[this]{shake_.end_message();}),
    movies_([this](auto name,auto){auto player=std::make_unique<movie::Player>(resources_.movie(name),mixer_);player->gain(1);return player;},{},0,[this](auto id){
        if(!progress_store_)throw script::Error("Missing native movie progress storage");progress_store_->state().set_flag(std::uint32_t(id),1);progress_store_->flush();}){
    environment_.reserve(432);foreground_.reserve(432);
    system_sounds_.prepare();
    preferences(preferences_);
}
void Scene::preferences(const Preferences& value){
    value.validate();auto dialogue=dialogue_.settings();dialogue.skin=value.skin;dialogue.window_alpha=value.window_alpha;dialogue.picture_alpha=value.picture_alpha;dialogue.button_alpha=value.button_alpha;dialogue.novel_alpha=value.novel_alpha;dialogue.text_step=value.text_step;dialogue.window=value.window;
    music_.settings(value.master,value.music);effects_.settings(value.master,value.effects);voices_.settings(value.master,value.speech,value.mute_adult);voices_.enabled(value.voice_enabled);voices_.character_switches(value.character_voice);system_sounds_.settings(value.master,value.effects);
    dialogue_.settings(dialogue);script::SelectionSettings selection;selection.skin=value.skin;selection.replay=replay_mode_;selection_.settings(selection);controls_.auto_delay(value.auto_ticks);skip_read_only_=value.skip_read_only;preferences_=value;
}
std::unique_ptr<audio::Voice> Scene::sound(std::int32_t id,bool loop,bool paused){
    auto resource=sound_resource(id);
    if(resource.ogg)return audio::play_ogg(*mixer_,{resource.source,loop?resource.source:nullptr},{},paused);
    return audio::play_wave(*mixer_,std::move(resource.source),loop,{},paused);
}
audio::SoundResource Scene::sound_resource(std::int32_t id){
    return audio::effect_resource(resources_,id);
}
std::unique_ptr<audio::Voice> Scene::music(std::int32_t id,bool loop){
    return audio::play_ogg(*mixer_,audio::music_resources(resources_,id,loop));
}
std::unique_ptr<audio::Voice> Scene::speech(const script::SpokenLine& line){
    auto source=audio::speech_resource(resources_,line.chapter,line.line,line.speaker);return audio::play_ogg(*mixer_,{source,line.loop?source:nullptr});
}
void Scene::start(std::string_view scene,std::int32_t label){session_.start(scene,label);reading_.enter(scene);choices_.clear();backlog_.clear();title_requested_=false;statistics_.scene=scene;}
script::CommandResult Scene::command(script::Command& call){
    statistics_.command=call.index;
    const auto invoke=[&]()->script::CommandResult{
    if(call.index==197){if(!call.arguments.empty())throw script::Error("Invalid GoTitle parameters");reading_.finish();title_requested_=true;return {true,true,{}};}
    if(call.index==237){if(!call.arguments.empty())throw script::Error("Invalid IfSkip parameters");reading_.force(2);return {true,false,{}};}
    if(call.index==238){if(!call.arguments.empty())throw script::Error("Invalid GetSkip2 parameters");const bool active=controls_.modes().demo_mode?(input_.skip || controls_.modes().latched_skip) && reading_.status(replay_mode_)!=0:skipping();return {true,false,script::Value(std::int32_t(active))};}
    if(call.index==222){if(!call.arguments.empty())throw script::Error("Invalid GetReplayMode parameters");return {true,false,script::Value(std::int32_t(replay_mode_))};}
    if(auto value=builtins_.dispatch(call))return *value;
    if(call.index==171 || call.index==172){const auto chroma=controls_.modes().character_chroma;const auto effect=controls_.modes().character_effect;
        auto value=controls_.dispatch(call);if(chroma!=controls_.modes().character_chroma || effect!=controls_.modes().character_effect)characters_.invalidate_image_style();return *value;}
    if(auto value=controls_.dispatch(call)){voices_.adult_mode(controls_.modes().adult_mode!=0);return *value;}
    if(auto value=progress_.dispatch(call))return *value;
    if(auto value=choices_.dispatch(call))return *value;
    if(auto value=calendar_.dispatch(call))return *value;
    if(auto value=music_.dispatch(call))return *value;
    if(auto value=effects_.dispatch(call))return *value;
    if(auto value=voices_.dispatch(call))return *value;
    if(auto value=weather_.dispatch(call))return *value;
    if(auto value=shake_.dispatch(call))return *value;
    {const bool message=!call.continuation && (call.index==130 || call.index==131 || call.index==133);
        if(auto value=dialogue_.dispatch(call)){if(message)backlog_.message(session_.active()?session_.active()->program().name():statistics_.scene,call.arguments[1].integer(),call.arguments[0].text(),dialogue_.name(),dialogue_.speaker(),dialogue_.novel().enabled(),controls_.modes().log_output==0);return *value;}}
    if(auto value=colour_.dispatch(call))return *value;
    if(auto value=bitmaps_.dispatch(call))return *value;
    if(auto value=characters_.dispatch(call))return *value;
    if(auto value=backgrounds_.dispatch(call,controls_.modes().time)){dialogue_.picture(backgrounds_.picture());return *value;}
    if(auto value=movies_.dispatch(call))return *value;
    throw script::Unsupported("Native game command is not implemented: "+std::to_string(call.index)+" "+library_.functions().at(call.index).name+
        " in scene "+(session_.active()?session_.active()->program().name():std::string()));
    };
    try{return invoke();}catch(const script::Unsupported& error){
        std::ostringstream context;context<<error.what()<<"; command "<<call.index<<" "<<library_.functions().at(call.index).name<<" in "
            <<(session_.active()?session_.active()->program().name():std::string())<<" (";
        for(const auto& argument:call.arguments){if(argument.type()==script::Type::real)context<<argument.real();
            else if(argument.type()==script::Type::text){const auto& value=argument.text();if(value.size()<64 && std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=32 && c<127;}))context<<std::quoted(value);else context<<"<text>";}
            else context<<argument.integer();context<<' ';}
        context<<')';throw script::Unsupported(context.str());
    }
}
script::RunState Scene::update(std::int64_t monotonic_us,Input input){
    timeline_.sample(monotonic_us);
    if(title_requested_)return script::RunState::stopped;
    if(timeline_.paused())return script::RunState::yielded;
    if(dialogue_.speaker()==-1)backlog_.clear_voice();
    selection_.sync(choices_);if(input.choice>=0){if(replay_mode_ && !choices_.previously_chosen(std::uint32_t(input.choice)))system_sounds_.play(audio::SystemCue::reject);else choose(std::uint32_t(input.choice));}
    else if(auto selected=selection_.input(input.selection))choose(*selected);
    input_=input;milliseconds_=timeline_.milliseconds();++statistics_.ticks;
    if(input_.toggle_window){window_visible_=!window_visible_;dialogue_.visible(window_visible_);}
    if(input_.advance){window_visible_=true;dialogue_.visible(true);dialogue_.advance();}
    if(dialogue_.phase()==script::DialoguePhase::waiting && controls_.automatic_advance())dialogue_.advance();
    const auto duration=[this](std::int32_t frames){return skipping()?0:frames==-1?15:frames==-2?30:frames;};
    movies_.update(monotonic_us);music_.update(duration);effects_.update(duration);voices_.update(duration);weather_.update();shake_.update(duration);
    backgrounds_.update(duration);colour_.update(duration);bitmaps_.update(duration);characters_.update(duration);dialogue_.update(skipping());
    calendar_.update([this](auto frames){return skipping()?frames/3:frames;});
    if(background_changing_ && !backgrounds_.busy()){weather_.background_finished(backgrounds_.picture());bitmaps_.clear_retired();characters_.clear_retired();background_changing_=false;}
    const auto state=session_.advance([this](auto& call){return command(call);});captions_.update(std::uint64_t(monotonic_us)/1000);selection_.sync(choices_);selection_.update(duration);
    if(session_.active())statistics_.scene=session_.active()->program().name();
    statistics_.voices=voices_.played();
    controls_.take_draw_request();return title_requested_?script::RunState::stopped:state;
}
void Scene::pause(bool value,std::int64_t monotonic_us){
    suspend(value,value,monotonic_us);
}
void Scene::suspend(bool logic,bool audio,std::int64_t now){
    if(audio && !logic)throw std::invalid_argument("Native audio suspension requires logic suspension");timeline_.pause(logic,now);
    if(audio)mixer_->pause(true);movies_.pause(audio,now);captions_.pause(audio,std::uint64_t(now)/1000);if(!audio)mixer_->pause(false);
}
void Scene::ambient_update(std::int64_t now){
    if(!timeline_.paused())throw std::logic_error("Native ambient update requires suspended story logic");timeline_.sample(now);if(mixer_->paused())return;
    music_.update();effects_.update();voices_.update();movies_.update(now);captions_.update(std::uint64_t(now)/1000);
}
void Scene::choose(std::uint32_t ordinal){
    if(ordinal>=choices_.items().size())throw script::Error("Native choice ordinal is out of range");const auto text=choices_.items()[ordinal].text;
    choices_.choose(ordinal);backlog_.message(session_.active()?session_.active()->program().name():statistics_.scene,-1,text,{},dialogue_.speaker(),dialogue_.novel().enabled(),controls_.modes().log_output==0,true);
}
bool Scene::can_accelerate()const{
    if(timeline_.paused() || title_requested_ || !skipping() || choices_.active() || movies_.active() || calendar_.busy())return false;
    if(backgrounds_.loading() || bitmaps_.loading() || characters_.loading() || weather_.loading() || effects_.loading() || voices_.loading())return false;
    if(dialogue_.phase()!=script::DialoguePhase::closed && !dialogue_.ready())return false;
    for(const auto& slot:music_.slots())if(slot.voice && !slot.voice->ready())return false;
    return true;
}
FrameBatch Scene::advance_frame(std::int64_t now,Input input,const std::function<std::int64_t()>& clock,FrameBudget budget){
    if(!clock)throw std::invalid_argument("Missing native frame budget clock");const auto continued=input.continued();
    return run_frame_batch([&](bool first){const auto state=update(now,first?input:continued);return FrameTick{state,can_accelerate()};},clock,budget);
}
script::BackgroundFrame Scene::background()const{
    auto frame=backgrounds_.frame();frame.transform=shake_.world();const auto channels=colour_.channels();if(frame.previous)frame.previous->colour=channels;if(frame.current)frame.current->colour=channels;return frame;
}
std::span<const image::Sprite> Scene::environment(){
    environment_.clear();const auto add=[this](auto items){environment_.insert(environment_.end(),items.begin(),items.end());};
    for(auto item:weather_.sprites())if(item.layer<0x1000){if(background_changing_ && !backgrounds_.picture() && weather_.scope()==0)item.world=image::SpriteWorld::previous;environment_.push_back(std::move(item));}
    add(bitmaps_.sprites(0,255,0));for(auto item:characters_.sprites()){
        if(background_changing_ && !preserve_characters_ && item.world!=image::SpriteWorld::previous)item.world=image::SpriteWorld::current;environment_.push_back(std::move(item));}
    std::stable_sort(environment_.begin(),environment_.end(),[](const auto& a,const auto& b){return a.layer==b.layer?a.order<b.order:a.layer<b.layer;});
    const auto colour=colour_.channels();for(auto& item:environment_){item.red=std::uint8_t(unsigned(item.red)*unsigned(std::min(colour[0]*2,255))/255);
        item.green=std::uint8_t(unsigned(item.green)*unsigned(std::min(colour[1]*2,255))/255);item.blue=std::uint8_t(unsigned(item.blue)*unsigned(std::min(colour[2]*2,255))/255);}
    return environment_;
}
std::span<const image::Sprite> Scene::foreground(){
    foreground_.clear();const auto add=[this](auto items){foreground_.insert(foreground_.end(),items.begin(),items.end());};
    for(const auto& item:weather_.sprites())if(item.layer>=0x1000)foreground_.push_back(item);add(bitmaps_.sprites(0,255,1));
    std::stable_sort(foreground_.begin(),foreground_.end(),[](const auto& a,const auto& b){return a.layer==b.layer?a.order<b.order:a.layer<b.layer;});return foreground_;
}
}
