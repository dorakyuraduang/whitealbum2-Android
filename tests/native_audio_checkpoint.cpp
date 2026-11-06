#include "scene_music.hpp"
#include "scene_effects.hpp"
#include "scene_voices.hpp"
#include "checkpoint_codec.hpp"
#include <fstream>
#include <iostream>

namespace s=wa2::script;namespace a=wa2::audio;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
using Bytes=std::vector<std::uint8_t>;
struct State {int id{},volume{};bool loop{},paused{};unsigned stops{},unpauses{};s::SpokenLine line;};
struct Voice:a::Voice {std::shared_ptr<State> state;explicit Voice(std::shared_ptr<State> value):state(std::move(value)){}bool ready()const override{return true;}bool finished()const override{return false;}std::string failure()const override{return {};}void gain(std::uint8_t v)override{state->volume=v;}void stop()override{++state->stops;}void pause(bool v)override{state->paused=v;if(!v)++state->unpauses;}};
struct Factory {std::vector<std::shared_ptr<State>> made;int failing{-1};std::unique_ptr<a::Voice> make(int id,bool loop,bool paused=false){if(id==failing)throw std::runtime_error("synthetic factory failure");auto state=std::make_shared<State>();state->id=id;state->loop=loop;state->paused=paused;made.push_back(state);return std::make_unique<Voice>(state);}};
Bytes patch(Bytes b,unsigned at,unsigned value){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(value>>(8*n));const auto crc=wa2::checkpoint::crc(std::span(b).first(b.size()-4));for(unsigned n=0;n<4;++n)b[b.size()-4+n]=std::uint8_t(crc>>(8*n));return b;}
template<class T>s::CommandResult invoke(T& controller,unsigned id,std::initializer_list<s::Value> args){std::vector<s::Value> values(args);s::Command command{id,values};return *controller.dispatch(command);}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},seen{};Factory music_factory;s::SceneMusic music([&](int id,bool loop){return music_factory.make(id,loop);},[&](auto){++seen;}),restored_music([&](int id,bool loop){return music_factory.make(id,loop);},[&](auto){throw std::runtime_error("restore must not mark music seen");});
    const auto no_music=music.checkpoint();restored_music.restore(no_music);CHECK(restored_music.song()==-1 && music_factory.made.empty());++cases;
    music.play(2,4,0,200);music.update();music.update();music.volume(64,3);music.update();restored_music.settings(128,128);auto saved_music=music.checkpoint();restored_music.restore(saved_music);CHECK(restored_music.checkpoint()==saved_music && restored_music.current().voice && music_factory.made.back()->id==2 && music_factory.made.back()->loop && music_factory.made.back()->volume==16 && seen==1);++cases;
    music.play(3,0,1,180);saved_music=music.checkpoint();restored_music.restore(saved_music);CHECK(!music_factory.made.back()->loop && restored_music.current().target==180);++cases;
    music.stop(30);restored_music.restore(music.checkpoint());CHECK(restored_music.song()==-1 && !restored_music.current().voice);++cases;
    music.play(7,0,0,220);saved_music=music.checkpoint();restored_music.restore(saved_music);const auto old_music=music_factory.made.back();
    auto corrupt=saved_music;corrupt.back()^=1;rejects([&]{restored_music.restore(corrupt);});CHECK(restored_music.checkpoint()==saved_music && !old_music->stops);++guards;
    for(auto [at,value]:std::array<std::pair<unsigned,unsigned>,3>{{{12,200},{16,2},{20,256}}}){rejects([&]{restored_music.restore(patch(saved_music,at,value));});CHECK(restored_music.checkpoint()==saved_music && !old_music->stops);++guards;}
    music_factory.failing=7;rejects([&]{restored_music.restore(saved_music);});CHECK(restored_music.checkpoint()==saved_music && !old_music->stops);music_factory.failing=-1;++guards;
    Factory effects_factory;unsigned effect_starts{},effect_stops{};s::SceneEffects effects([&](auto id,auto loop){return effects_factory.make(id,loop);},{},[&](auto id,auto loop,auto paused){return effects_factory.make(id,loop,paused);});
    s::SceneEffects restored_effects([&](auto id,auto loop){return effects_factory.make(id,loop);},{},[&](auto id,auto loop,auto paused){return effects_factory.make(id,loop,paused);},{[&](auto,auto,const auto&){++effect_starts;},[&](auto){++effect_stops;}});
    const auto no_effects=effects.checkpoint();restored_effects.restore(no_effects);CHECK(!effect_starts && !effect_stops);++cases;
    effects.once(10,255);effects.play(0,11,0,0,128);CHECK(effects.checkpoint()==no_effects);++cases;
    effects.play(2,100,0,1,100);effects.volume(2,80,4);effects.update();effects.play(15,101,0,1,200,1);CHECK(effects.slot(15).prepared && effects.slot(15).target==200);const auto saved_effects=effects.checkpoint();++cases;
    restored_effects.play(0,88,0,0,255);const auto ordinary=effects_factory.made.back();restored_effects.once(89,255);const auto once=effects_factory.made.back();restored_effects.settings(128,256);const auto starts=effect_starts;restored_effects.restore(saved_effects);
    CHECK(ordinary->stops==1 && once->stops==1 && effect_starts==starts+2 && restored_effects.checkpoint()==saved_effects && !restored_effects.slot(0).voice && !restored_effects.slot(15).prepared && restored_effects.slot(2).duration==-2);++cases;
    const auto prepared=effects_factory.made.back();CHECK(prepared->id==101 && prepared->loop && !prepared->paused && prepared->unpauses==1 && prepared->volume==0);++cases;
    for(unsigned n=0;n<31;++n)restored_effects.update([](auto ticks){return ticks==-2?30:ticks;});CHECK(restored_effects.slot(2).volume==80 && restored_effects.slot(15).volume==200 && prepared->volume==100);++cases;
    const auto effect_calls=effects_factory.made.size();const auto effect_events=effect_starts;for(auto [at,value]:std::array<std::pair<unsigned,unsigned>,3>{{{12,2},{24,0xffffffff},{28,256}}}){rejects([&]{restored_effects.restore(patch(saved_effects,at,value));});CHECK(restored_effects.checkpoint()==saved_effects && effects_factory.made.size()==effect_calls && effect_starts==effect_events);++guards;}
    effects_factory.failing=101;const auto old_prepared_stops=prepared->stops;rejects([&]{restored_effects.restore(saved_effects);});CHECK(restored_effects.checkpoint()==saved_effects && prepared->stops==old_prepared_stops && effects_factory.made.back()->id==100 && effects_factory.made.back()->stops==1 && effect_starts==effect_events);effects_factory.failing=-1;++guards;
    s::SceneEffects missing_prepare([&](auto id,auto loop){return effects_factory.make(id,loop);});rejects([&]{missing_prepare.restore(saved_effects);});++guards;
    restored_effects.restore(no_effects);CHECK(!restored_effects.slot(2).voice && !restored_effects.slot(15).voice);++cases;
    Factory voice_factory;unsigned voice_started{},voice_stopped{},voice_issued{};const auto speech=[&](const s::SpokenLine& line){auto result=voice_factory.make(line.line,line.loop);voice_factory.made.back()->line=line;return result;};
    s::SceneVoices voices(speech,[]{return 1001;},[]{return 2;},[]{return 10;});s::SceneVoices restored_voices(speech,[]{return 9999;},[]{return 99;},[]{return 500;},{},{},{[&](auto,const auto&,const auto&){++voice_started;},[&](auto){++voice_stopped;},[&](auto,const auto&,auto){++voice_issued;}});
    const auto no_voices=voices.checkpoint();restored_voices.restore(no_voices);CHECK(!restored_voices.active() && !voice_started);++cases;
    invoke(voices,139,{s::Value(2),s::Value(1001),s::Value(0),s::Value(250),s::Value(5),s::Value(0)});const auto ordinary_speech=voices.checkpoint();restored_voices.restore(ordinary_speech);CHECK(!restored_voices.active() && restored_voices.played()==1);++cases;
    invoke(voices,139,{s::Value(1),s::Value(2001),s::Value(-1),s::Value(200),s::Value(7),s::Value(1)});invoke(voices,139,{s::Value(0),s::Value(2001),s::Value(1),s::Value(180),s::Value(8),s::Value(2)});invoke(voices,139,{s::Value(3),s::Value(2001),s::Value(1),s::Value(160),s::Value(9),s::Value(3)});
    invoke(voices,235,{s::Value(64),s::Value(10),s::Value(1)});for(unsigned n=0;n<3;++n)voices.update();const auto saved_voices=voices.checkpoint();const auto made_before=voice_factory.made.size();restored_voices.settings(128,128);restored_voices.restore(saved_voices);
    CHECK(restored_voices.checkpoint()==saved_voices && restored_voices.active()==3 && restored_voices.played()==4 && !voice_issued && voice_started==3 && voice_factory.made.size()==made_before+3);++cases;
    const auto first_loop=voice_factory.made[made_before];CHECK(first_loop->line.speaker==1 && first_loop->line.chapter==2001 && first_loop->line.line==17 && first_loop->line.loop && first_loop->volume==50);++cases;
    restored_voices.settings(128,128,true);restored_voices.adult_mode(true);restored_voices.restore(saved_voices);CHECK(restored_voices.active()==2 && !voice_issued);++cases;restored_voices.adult_mode(false);restored_voices.restore(saved_voices);CHECK(restored_voices.checkpoint()==saved_voices);++cases;
    for(auto [at,value]:std::array<std::pair<unsigned,unsigned>,5>{{{20,2},{28,100},{32,10000},{36,10000},{40,256}}}){rejects([&]{restored_voices.restore(patch(saved_voices,at,value));});CHECK(restored_voices.checkpoint()==saved_voices);++guards;}
    const auto events_before=voice_started;const auto old_loop=voice_factory.made.back();voice_factory.failing=19;rejects([&]{restored_voices.restore(saved_voices);});CHECK(restored_voices.checkpoint()==saved_voices && !old_loop->stops && voice_started==events_before && voice_factory.made.back()->stops==1);voice_factory.failing=-1;++guards;
    restored_voices.restore(no_voices);CHECK(!restored_voices.active() && !restored_voices.played() && voice_stopped>0 && !voice_issued);++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-audio-checkpoint.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"looping_audio_restarted\":true,\"one_shots_not_replayed\":true,\"restore_history_events\":0,\"fake_voices_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS native music, looping effect and looping speech load specifications.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
