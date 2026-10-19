#include "scene.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <charconv>
#include <sstream>
#include <iomanip>

namespace wa2::game {
Scene::Scene(resource::BankOpener opener,std::uint32_t seed)
    :resources_(std::move(opener)),mixer_(std::make_shared<audio::Mixer>()),fonts_(resources_.required("fon")),
    library_(resources_.required("ck-gal")),session_(library_),random_(seed),
    controls_([this]{return milliseconds_;},[this]{return input_.advance || skipping();},[this]{return input_.advance || skipping();},[this]{return skipping();}),
    music_([this](auto id,bool loop){return music(id,loop);}),
    effects_([this](auto id,bool loop){return sound(id,loop);},[this]{return skipping();},[this](auto id,bool loop,bool paused){return sound(id,loop,paused);}),
    voices_([this](const auto& line){return speech(line);},[this]{
        if(controls_.modes().voice_chapter!=-1)return controls_.modes().voice_chapter;
        std::int32_t chapter{};const auto& name=session_.active()?session_.active()->program().name():statistics_.scene;const auto value=std::from_chars(name.data(),name.data()+name.size(),chapter);
        if(value.ec!=std::errc{} || value.ptr!=name.data()+name.size())throw script::Error("scene has no numeric voice chapter");return chapter;
    },[this]{return dialogue_.speaker();},[this]{return controls_.modes().voice_offset;},[this]{return skipping();},[this]{return input_.advance;}),
    weather_([this](auto name){return resources_.image("grp",name);},[this]{return random_.next();}),
    backgrounds_([this](auto name){return resources_.image("bak",name);},[this](bool){background_changing_=true;bitmaps_.retire_background();characters_.retire_background();},
        [this](auto name){return resources_.image("grp",name);},[this](auto name){return resources_.image("grp",name);}),
    bitmaps_([this](auto name,auto group,auto){return resources_.image(group,name);}),
    characters_([this](auto name){return resources_.image("char",name);}),
    dialogue_(fonts_,[this](auto name){return resources_.image("grp",name);},[this](auto){++statistics_.messages;}),
    movies_([this](auto name,auto){auto player=std::make_unique<movie::Player>(resources_.movie(name),mixer_);player->gain(1);return player;}){
    environment_.reserve(432);foreground_.reserve(432);
}
std::unique_ptr<audio::Voice> Scene::sound(std::int32_t id,bool loop,bool paused){
    std::array<char,32> name{};
    // Existing 0x4058f0 tries root Ogg, root WAV, then IC Ogg/IC WAV.
    for(bool introductory:{false,true})if(const auto* bank=resources_.optional("se",introductory)){
        std::snprintf(name.data(),name.size(),"SE_%04d.ogg",int(id));auto index=bank->find_first(name.data());
        if(index!=resource::Archive::missing){auto source=bank->open_member(index);return audio::play_ogg(*mixer_,{source,loop?source:nullptr},{},paused);}
        std::snprintf(name.data(),name.size(),"SE_%04d.wav",int(id));index=bank->find_first(name.data());
        if(index!=resource::Archive::missing)return audio::play_wave(*mixer_,bank->open_member(index),loop,{},paused);
    }
    throw resource::Error("missing original sound effect "+std::to_string(id));
}
std::unique_ptr<audio::Voice> Scene::music(std::int32_t id,bool loop){
    std::array<char,32> name{};
    // Existing 0x461090 resolves single/intro inside a bank before falling back
    // to IC. An I/O failure is propagated, never treated as an absent member.
    for(bool introductory:{false,true})if(const auto* bank=resources_.optional("bgm",introductory)){
        std::snprintf(name.data(),name.size(),"BGM_%03d.ogg",int(id));const auto full=bank->find_first(name.data());
        std::snprintf(name.data(),name.size(),"BGM_%03d_a.ogg",int(id));
        if(full!=resource::Archive::missing || bank->find_first(name.data())!=resource::Archive::missing)
            return audio::play_ogg(*mixer_,audio::music_playlist(*bank,id,loop));
    }
    throw resource::Error("missing original background music "+std::to_string(id));
}
std::unique_ptr<audio::Voice> Scene::speech(const script::SpokenLine& line){
    std::array<char,32> name{};std::snprintf(name.data(),name.size(),"%04d_%04d_%02d.ogg",int(line.chapter),int(line.line),int(line.speaker));
    for(bool introductory:{false,true})if(const auto* bank=resources_.optional("voice",introductory)){
        const auto index=bank->find_first(name.data());if(index!=resource::Archive::missing){auto source=bank->open_member(index);return audio::play_ogg(*mixer_,{source,line.loop?source:nullptr});}
    }
    throw resource::Error("missing original speech "+std::string(name.data()));
}
void Scene::start(std::string_view scene,std::int32_t label){session_.start(scene,label);statistics_.scene=scene;}
script::CommandResult Scene::command(script::Command& call){
    statistics_.command=call.index;
    const auto invoke=[&]()->script::CommandResult{
    if(auto value=controls_.dispatch(call))return *value;
    if(auto value=music_.dispatch(call))return *value;
    if(auto value=effects_.dispatch(call))return *value;
    if(auto value=voices_.dispatch(call))return *value;
    if(auto value=weather_.dispatch(call))return *value;
    if(auto value=dialogue_.dispatch(call))return *value;
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
    input_=input;milliseconds_=std::uint32_t(std::uint64_t(monotonic_us)/1000);++statistics_.ticks;
    if(input_.toggle_window){window_visible_=!window_visible_;dialogue_.visible(window_visible_);}
    if(input_.advance){window_visible_=true;dialogue_.visible(true);dialogue_.advance();}
    const auto duration=[this](std::int32_t frames){return skipping()?0:frames==-1?15:frames==-2?30:frames;};
    movies_.update(monotonic_us);music_.update(duration);effects_.update(duration);voices_.update(duration);weather_.update();
    backgrounds_.update(duration);colour_.update(duration);bitmaps_.update(duration);characters_.update(duration);dialogue_.update(skipping());
    if(background_changing_ && !backgrounds_.busy()){weather_.stop(true);bitmaps_.clear_retired();characters_.clear_retired();background_changing_=false;}
    const auto state=session_.advance([this](auto& call){return command(call);});
    if(session_.active())statistics_.scene=session_.active()->program().name();
    statistics_.voices=voices_.played();
    controls_.take_draw_request();return state;
}
script::BackgroundFrame Scene::background()const{
    auto frame=backgrounds_.frame();const auto channels=colour_.channels();if(frame.previous)frame.previous->colour=channels;if(frame.current)frame.current->colour=channels;return frame;
}
std::span<const image::Sprite> Scene::environment(){
    environment_.clear();const auto add=[this](auto items){environment_.insert(environment_.end(),items.begin(),items.end());};
    if(weather_.scope()==0)add(weather_.sprites());add(bitmaps_.sprites(0,255,0));add(characters_.sprites());
    std::sort(environment_.begin(),environment_.end(),[](const auto& a,const auto& b){return a.layer==b.layer?a.order<b.order:a.layer<b.layer;});
    const auto colour=colour_.channels();for(auto& item:environment_){item.red=std::uint8_t(unsigned(item.red)*unsigned(std::min(colour[0]*2,255))/255);
        item.green=std::uint8_t(unsigned(item.green)*unsigned(std::min(colour[1]*2,255))/255);item.blue=std::uint8_t(unsigned(item.blue)*unsigned(std::min(colour[2]*2,255))/255);}
    return environment_;
}
std::span<const image::Sprite> Scene::foreground(){
    foreground_.clear();const auto add=[this](auto items){foreground_.insert(foreground_.end(),items.begin(),items.end());};
    if(weather_.scope()!=0)add(weather_.sprites());add(bitmaps_.sprites(0,255,1));
    std::sort(foreground_.begin(),foreground_.end(),[](const auto& a,const auto& b){return a.layer==b.layer?a.order<b.order:a.layer<b.layer;});return foreground_;
}
}
