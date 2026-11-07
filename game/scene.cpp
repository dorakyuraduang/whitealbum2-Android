#include "scene.hpp"
#include "audio_resources.hpp"
#include "options_audio.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <charconv>
#include <sstream>
#include <iomanip>

namespace wa2::game {
Scene::Scene(resource::BankOpener opener,std::uint32_t seed,std::shared_ptr<progress::Store> progress,SceneServices services)
    :resources_(std::move(opener)),services_(std::move(services)),mixer_(std::make_shared<audio::Mixer>()),system_sounds_(*mixer_,[this](auto id){return sound_resource(id);}),fonts_(resources_.members("fon")),
    library_(resources_.members("ck-gal")),session_(library_),random_(seed),progress_store_(std::move(progress)),reading_(progress_store_),builtins_(random_),
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
        [this](auto name){return resources_.image("grp",name);},[this](auto name){return resources_.image("grp",name,controls_.modes().background_chroma,controls_.modes().background_effect);},[this](auto bank,auto index){progress_.event_image(bank,index);}),
    bitmaps_([this](auto name,auto group,auto){return resources_.image(group,name);},[this](auto name,auto group){return image::Animation::parse(resources_.data(group,name));},[this](auto name){progress_.image_file(name);}),
    characters_([this](auto name){const auto& mode=controls_.modes();std::string_view automatic=mode.automatic_palette;
        if(automatic.empty() || !resources_.contains("grp",automatic)){switch(mode.time){case 0:automatic={};break;case 1:automatic="evening.amp";break;case 2:automatic="night.amp";break;case 3:automatic="indoor.AMP";break;default:throw script::Unsupported("Automatic character palette for time mode "+std::to_string(mode.time));}}
        return resources_.image("char",name,mode.character_chroma,automatic,mode.character_effect);}),
    dialogue_(fonts_,[this](auto name){return resources_.image("grp",name);},[this](auto line){
        reading_.seen(session_.active()?session_.active()->program().name():statistics_.scene,line);controls_.reset_message_timer();++statistics_.messages;},[this]{shake_.end_message();}),
    movies_([this](auto name,auto record)->std::unique_ptr<movie::Playback>{return services_.movie?services_.movie(name,record):std::make_unique<movie::Player>(resources_.movie(name),mixer_);},[this]{
        // Cached SetMovie 0x413a1c stops speech channel 0, then 0x413920
        // stops all ordinary/channel effects. Other voices, BGM and visual
        // state remain live; 0x413bf0 is a different full-reset path.
        voices_.stop(0);effects_.stop_all();},0,[this](auto id){
        if(!progress_store_)throw script::Error("Missing native movie progress storage");progress_store_->state().set_flag(std::uint32_t(id),1);progress_store_->flush();},[this](auto id){
        if(!progress_store_)throw script::Error("Missing native movie skip progress storage");return progress_store_->state().flag(std::uint32_t(id))!=0;}){
    environment_.reserve(432);foreground_.reserve(432);
    system_sounds_.prepare();
    preferences(preferences_);
}
void Scene::preferences(const Preferences& value){
    value.validate();auto dialogue=dialogue_.settings();dialogue.skin=value.skin;dialogue.window_alpha=value.window_alpha;dialogue.picture_alpha=value.picture_alpha;dialogue.button_alpha=value.button_alpha;dialogue.novel_alpha=value.novel_alpha;dialogue.text_step=value.text_step;dialogue.window=value.window;
    music_.settings(value.effective_master(),value.effective_music());effects_.settings(value.effective_master(),value.effective_effects());voices_.settings(value.effective_master(),value.speech,value.mute_adult);voices_.enabled(value.voice_enabled);voices_.character_switches(value.character_voice);system_sounds_.settings(value.effective_master(),value.effective_effects());
    dialogue_.settings(dialogue);script::SelectionSettings selection;selection.skin=value.skin;selection.replay=replay_mode_;selection_.settings(selection);controls_.auto_delay(value.auto_ticks);skip_read_only_=value.skip_read_only;preferences_=value;
    movies_.file_suffix(unsigned(value.movie_quality));movies_.gain(value.movie_amplitude());
}
std::unique_ptr<audio::Voice> Scene::sound(std::int32_t id,bool loop,bool paused){
    if(services_.effects)return services_.effects(id,loop,paused);
    auto resource=sound_resource(id);
    if(resource.ogg)return audio::play_ogg(*mixer_,{resource.source,loop?resource.source:nullptr},{},paused);
    return audio::play_wave(*mixer_,std::move(resource.source),loop,{},paused);
}
audio::SoundResource Scene::sound_resource(std::int32_t id){
    return audio::effect_resource(resources_,id);
}
std::unique_ptr<audio::Voice> Scene::music(std::int32_t id,bool loop){
    if(services_.music)return services_.music(id,loop);
    return audio::play_ogg(*mixer_,audio::music_resources(resources_,id,loop));
}
std::unique_ptr<audio::Voice> Scene::speech(const script::SpokenLine& line){
    if(services_.speech)return services_.speech(line);
    auto source=audio::speech_resource(resources_,line.chapter,line.line,line.speaker);return audio::play_ogg(*mixer_,{source,line.loop?source:nullptr});
}
void Scene::options_preview_effects(){game::options_preview_effects(effects_,preferences_);}
void Scene::options_preview_voice(){game::options_preview_voice(voices_,random_,preferences_);}
void Scene::options_reset_sound(){game::options_reset_sound(effects_);}
std::optional<script::StoryControl> Scene::control_at(float x,float y)const{
    if(movies_.active())return {};
    const auto point=image::scene_point(x,y,shake_.dialogue());const auto result=point?dialogue_.control_at((*point)[0],(*point)[1]):std::nullopt;
    return controls_.modes().demo_mode && result!=script::StoryControl::automatic?std::nullopt:result;
}
bool Scene::control_allowed(script::StoryControl control)const{
    if(unsigned(control)>unsigned(script::StoryControl::hide))throw script::Error("Unknown native story control");
    if(movies_.active() || (controls_.modes().demo_mode && control!=script::StoryControl::automatic))return false;
    return !replay_mode_ || (control!=script::StoryControl::quick_save && control!=script::StoryControl::quick_load && control!=script::StoryControl::save && control!=script::StoryControl::load);
}
void Scene::control_input(const script::SelectionInput& input){
    if(movies_.active())return;
    script::StoryToolbar state;state.hover=input.pointer?control_at(input.x,input.y):std::nullopt;
    state.near_bottom=input.pointer && input.y>=664;state.automatic=controls_.modes().auto_mode;state.skipping=controls_.modes().latched_skip;
    state.demo=controls_.modes().demo_mode!=0;state.replay=replay_mode_!=0;state.pressed=input.activate && state.hover && control_allowed(*state.hover);
    if(dialogue_.toolbar(state) && state.hover && control_allowed(*state.hover))system_sounds_.play(audio::SystemCue::focus);
    if(input.activate && state.hover && *state.hover!=script::StoryControl::automatic)cancel_automatic();
}
void Scene::story_control(script::StoryControl control){
    if(!control_allowed(control)){system_sounds_.play(audio::SystemCue::reject);return;}
    switch(control){
    case script::StoryControl::skip:controls_.latched_skip(!controls_.modes().latched_skip);break;
    case script::StoryControl::automatic:controls_.auto_mode(!controls_.modes().auto_mode,preferences_.auto_ticks);break;
    case script::StoryControl::hide:window_visible_=!window_visible_;dialogue_.visible(window_visible_);break;
    default:throw script::Error("Native scene control requires its application owner");
    }
}
std::int32_t Scene::theme()const{
    for(const auto& variable:library_.globals().variables())if(variable.name=="FLG_00"){
        const auto value=variable.value.integer();if(value<0 || value>6)throw script::Unsupported("Native story theme is out of range");return value;
    }
    throw script::Error("Missing native story theme global FLG_00");
}
std::uint32_t Scene::save_story_code()const{
    const auto flag=[&](std::string_view name){for(const auto& variable:library_.globals().variables())if(variable.name==name)return variable.value.integer();throw script::Error("Missing native save classification global");};
    const auto& date=calendar_.date();const auto theme=flag("FLG_00"),sub=flag("FLG_\x8f\xcd\x8a\xc7\x97\x9d\x83T\x83u");
    const auto code=std::int64_t(date.month*100+date.day)*100+std::int64_t(theme)*10+sub;
    if(theme<0 || theme>6 || sub<0 || sub>9 || code<0 || code>9999999)throw script::Unsupported("Native save story classification is out of range");return std::uint32_t(code);
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
        const bool append=message && call.arguments.size()==3 && call.arguments[2].integer()==0 && !dialogue_.raw_text().empty();
        if(auto value=dialogue_.dispatch(call)){if(message){if(append)backlog_.append(call.arguments[0].text());else backlog_.message(session_.active()?session_.active()->program().name():statistics_.scene,call.arguments[1].integer(),call.arguments[0].text(),dialogue_.name(),dialogue_.speaker(),dialogue_.novel().enabled(),controls_.modes().log_output==0);}return *value;}}
    if(auto value=colour_.dispatch(call))return *value;
    if(auto value=bitmaps_.dispatch(call))return *value;
    if(auto value=characters_.dispatch(call))return *value;
    {const bool first=!call.continuation;
        if(auto value=backgrounds_.dispatch(call,controls_.modes().time)){
            if(first && (call.index==146 || call.index==147 || call.index==225 || call.index==226) && call.arguments[1].integer()!=-2){std::array<char,32> name{};std::snprintf(name.data(),name.size(),"B%04d%d%d.amp",int(call.arguments[1].integer()),int(call.arguments[2].integer()),int(controls_.modes().time));controls_.automatic_palette(name.data());}
            dialogue_.picture(backgrounds_.picture());return *value;
        }
    }
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
    if(movies_.active()){
        // Cached 4337be..433885 bypasses the game update while a movie plays.
        // Device audio and the movie clock continue; hidden scene controllers,
        // choices, toolbar and auto-text must not consume that frame's input.
        const bool requested=input.advance || input.toggle_window || input.cancel || input.selection.activate || input.selection.confirm;
        if(requested && movies_.request_skip())cancel_automatic();else movies_.update(monotonic_us);
        input_={};milliseconds_=timeline_.milliseconds();captions_.update(std::uint64_t(monotonic_us)/1000);
        if(!movies_.playback()->finished())return script::RunState::yielded;
        if(!session_.active() || session_.active()->pending_command()!=193)throw script::Error("Native movie owner has no suspended SetMovie command");
        return session_.advance([this](auto& call){return command(call);});
    }
    // Cached 4187ab..4187dc follows the active script before running it.
    // Prelude waits/read-only skip must not retain the previous chapter's row.
    if(const auto active=session_.active())reading_.enter(active->program().name());
    if(dialogue_.speaker()==-1)backlog_.clear_voice();
    selection_.sync(choices_);if(input.choice>=0){if(replay_mode_ && !choices_.previously_chosen(std::uint32_t(input.choice)))system_sounds_.play(audio::SystemCue::reject);else choose(std::uint32_t(input.choice));}
    else if(auto selected=selection_.input(input.selection))choose(*selected);
    input_=input;milliseconds_=timeline_.milliseconds();++statistics_.ticks;
    if(input_.toggle_window || (input_.advance && !choices_.active()))cancel_automatic();
    if(input_.toggle_window){window_visible_=!window_visible_;dialogue_.visible(window_visible_);}
    if(input_.advance){window_visible_=true;dialogue_.visible(true);if(preferences_.cut_voice_on_advance && !controls_.modes().demo_mode && dialogue_.phase()==script::DialoguePhase::waiting)voices_.stop(0);dialogue_.advance();}
    if(dialogue_.phase()==script::DialoguePhase::waiting && controls_.automatic_advance())dialogue_.advance();
    const auto duration=[this](std::int32_t frames){return preferences_.transition_frames(frames,skipping());};
    movies_.update(monotonic_us);music_.update(duration);effects_.update(duration);voices_.update(duration);weather_.update();shake_.update(duration);
    backgrounds_.update(duration);colour_.update(duration);bitmaps_.update(duration);characters_.update(duration);dialogue_.update(skipping(),preferences_.transition_wait);
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
    if(!clock)throw std::invalid_argument("Missing native frame budget clock");const auto continued=input.continued();const bool movie_frame=movies_.active();
    return run_frame_batch([&](bool first){const auto state=update(now,first?input:continued);return FrameTick{state,!movie_frame && can_accelerate()};},clock,budget);
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
