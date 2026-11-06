#include "scene_captions.hpp"
#include "scene_effects.hpp"
#include "scene_voices.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>

namespace s=wa2::script;namespace sub=wa2::subtitles;namespace a=wa2::audio;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
struct State {bool ready{},done{},paused{};unsigned stops{};std::string failure;};
struct Voice:a::Voice {
    std::shared_ptr<State> state;
    explicit Voice(std::shared_ptr<State> s):state(std::move(s)){}
    bool ready()const override{return state->ready && !state->paused;}
    bool finished()const override{return state->done;}
    std::string failure()const override{return state->failure;}
    void gain(std::uint8_t)override{}
    void stop()override{state->done=true;++state->stops;}
    void pause(bool value)override{state->paused=value;}
};
template<class Controller>s::CommandResult invoke(Controller& controller,unsigned id,std::initializer_list<s::Value> args){std::vector<s::Value> values(args);s::Command call{id,values};return *controller.dispatch(call);}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};const auto tracks=sub::main_story_tracks();
    const auto effect=std::find_if(tracks.begin(),tracks.end(),[](const auto& t){return t.key<65536;});
    const auto speech=std::find_if(tracks.begin(),tracks.end(),[](const auto& t){return t.key>=65536;});CHECK(effect!=tracks.end() && speech!=tracks.end());
    s::SceneCaptions captions;auto flags=std::make_shared<State>();Voice voice(flags);std::uint64_t wall=1000;captions.update(wall);
    CHECK(captions.queue(effect->key,sub::Kind::effect,0,voice));captions.update(wall+=1000);CHECK(!captions.current());++cases;
    flags->ready=true;captions.update(wall+=1000);const auto started=wall;captions.update(wall=started+effect->cues.front().begin_ms);CHECK(captions.current()==&effect->cues.front());++cases;
    const auto sprites=captions.sprites();CHECK(sprites.size()==effect->cues.front().glyphs.size() && !sprites.empty());const auto atlas=sprites.front().image;
    for(const auto& item:sprites)CHECK(item.image==atlas && item.x>=0 && item.x+item.width<=1280 && item.y>=0 && item.y+item.height<=720 && item.source_x>=0 && item.source_y>=0 && item.source_x+item.source_width<=atlas->width+.001f && item.source_y+item.source_height<=atlas->height+.001f);
    const auto layouts=captions.layout_builds();captions.sprites();captions.sprites();CHECK(captions.layout_builds()==layouts);++cases;
    captions.pause(true,wall);captions.update(wall+=100000);CHECK(captions.current()==&effect->cues.front());captions.pause(false,wall);++cases;
    captions.stop(sub::Kind::voice,0);CHECK(captions.current()==&effect->cues.front());captions.stop(sub::Kind::effect,0);CHECK(!captions.current() && captions.sprites().empty());++cases;
    flags->ready=false;captions.queue(effect->key,sub::Kind::effect,0,voice);captions.stop(sub::Kind::effect,0);flags->ready=true;captions.update(++wall);CHECK(!captions.current());++cases;
    CHECK(!captions.queue(0xffffffffu,sub::Kind::effect,0,voice));CHECK(!captions.queue(effect->key,sub::Kind::voice,0,voice));++cases;
    rejects([&]{captions.queue(effect->key,sub::Kind::effect,16,voice);});rejects([&]{captions.queue(speech->key,sub::Kind::voice,4,voice);});guards+=2;
    rejects([&]{captions.update(wall-1);});++guards;
    flags->failure="synthetic decoder failure";captions.queue(effect->key,sub::Kind::effect,0,voice);rejects([&]{captions.update(++wall);});++guards;flags->failure.clear();captions.reset();
    // All embedded main-story tracks use the same native font/layout path.
    for(const auto& track:tracks){const auto kind=track.key<65536?sub::Kind::effect:sub::Kind::voice;captions.queue(track.key,kind,0,voice,true);captions.update(++wall);const auto epoch=wall;
        captions.update(wall=epoch+track.cues.front().begin_ms);CHECK(captions.current()==&track.cues.front() && !captions.sprites().empty() && captions.sprites().front().image==atlas);
        captions.update(wall=epoch+track.cues.back().end_ms);CHECK(!captions.current() && captions.sprites().empty());++cases;}
    unsigned starts{},stops{};std::vector<std::shared_ptr<State>> effect_states;
    const auto factory=[&](int,bool,bool paused=false)->std::unique_ptr<a::Voice>{auto state=std::make_shared<State>();state->ready=true;state->paused=paused;effect_states.push_back(state);return std::make_unique<Voice>(state);};
    {
        s::SceneEffects effects([&](int id,bool loop){return factory(id,loop);},{},factory,
            {[&](auto slot,auto id,const auto& transport){++starts;captions.queue(std::uint32_t(id),sub::Kind::effect,slot,transport);},[&](auto slot){++stops;captions.stop(sub::Kind::effect,slot);}});
        effects.play(0,int(effect->key),0,0,255,1);captions.update(++wall);CHECK(!starts && !captions.current());++cases;
        effects.play(0,int(effect->key),0,0,255,2);captions.update(++wall);const auto epoch=wall;captions.update(wall=epoch+effect->cues.front().begin_ms);CHECK(starts==1 && captions.current()==&effect->cues.front());++cases;
        effects.stop(0);CHECK(stops==1 && !captions.current() && effect_states.back()->stops==1);++cases;
        effects.play(0,int(effect->key),0,0,255);effects.play(1,int(effect->key),0,0,255);effects.stop(0,1);effects.stop(1,1);effects.update();CHECK(effects.slot(0).voice && effects.slot(1).voice);
        effects.update();CHECK(!effects.slot(0).voice && !effects.slot(1).voice && stops==3);++cases;
        effects.play(3,int(effect->key),0,0,255);effect_states.back()->done=true;effects.update();captions.update(++wall);CHECK(!captions.current());++cases;
    }
    captions.reset();unsigned spoken_starts{},spoken_stops{};std::shared_ptr<State> spoken_flags;
    {
        s::SceneVoices voices([&](const auto&)->std::unique_ptr<a::Voice>{spoken_flags=std::make_shared<State>();return std::make_unique<Voice>(spoken_flags);},[&]{return int(speech->key>>16);},[]{return 1;},{},{},{},
            {[&](auto slot,const auto& line,const auto& transport){++spoken_starts;captions.queue((std::uint32_t(line.chapter)<<16)|std::uint32_t(line.line),sub::Kind::voice,slot,transport);},[&](auto slot){++spoken_stops;captions.stop(sub::Kind::voice,slot);}});
        invoke(voices,138,{s::Value(1),s::Value(255),s::Value(0),s::Value(0),s::Value(int(speech->key&65535))});captions.update(++wall);CHECK(spoken_starts==1 && !captions.current());++cases;
        invoke(voices,141,{s::Value(0),s::Value(0)});CHECK(spoken_stops==1 && !captions.current());++cases;
        invoke(voices,138,{s::Value(1),s::Value(255),s::Value(0),s::Value(0),s::Value(int(speech->key&65535))});spoken_flags->ready=true;captions.update(++wall);const auto epoch=wall;
        captions.update(wall=epoch+speech->cues.front().begin_ms);CHECK(captions.current()==&speech->cues.front());spoken_flags->done=true;voices.update();CHECK(!captions.current() && spoken_stops==2);++cases;
    }
    captions.reset();flags->ready=true;captions.queue(effect->key,sub::Kind::effect,1,voice);captions.queue(speech->key,sub::Kind::voice,0,voice);captions.update(++wall);const auto epoch=wall;
    captions.update(wall=epoch+effect->cues.front().begin_ms);CHECK(captions.current()==&effect->cues.front());captions.stop(sub::Kind::effect,1);captions.update(++wall);CHECK(!captions.current());++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-caption-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"embedded_tracks\":"<<tracks.size()<<",\"synthetic_audio_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";out.close();CHECK(out);
    std::cout<<"PASS native caption readiness, font cache and audio lifecycle events.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
