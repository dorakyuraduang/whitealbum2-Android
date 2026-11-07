#include "legacy_save.hpp"
#include "scene_music.hpp"
#include "scene_effects.hpp"
#include "scene_voices.hpp"
#include "checkpoint_codec.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>

namespace g=wa2::game;namespace s=wa2::script;namespace a=wa2::audio;namespace c=wa2::checkpoint;
using Bytes=std::vector<std::uint8_t>;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
void put(Bytes& bytes,std::size_t at,std::uint32_t value){for(unsigned n=0;n<4;++n)bytes.at(at+n)=std::uint8_t(value>>(n*8));}
void short_word(Bytes& bytes,std::size_t at,unsigned value){bytes.at(at)=std::uint8_t(value);bytes.at(at+1)=std::uint8_t(value>>8);}
Bytes scene(){Bytes bytes(g::original_scene_v2_bytes);for(unsigned n=0;n<4;++n)put(bytes,0xefc+n*4,0xffffffff);return bytes;}
struct State {std::int32_t channel{},id{},speaker{},chapter{},line{};bool loop{},paused{},stopped{};std::uint8_t gain{};};
struct Voice:a::Voice {std::shared_ptr<State> state;explicit Voice(std::shared_ptr<State> value):state(std::move(value)){}bool ready()const override{return true;}bool finished()const override{return state->stopped;}std::string failure()const override{return {};}void gain(std::uint8_t value)override{state->gain=value;}void stop()override{state->stopped=true;}void pause(bool value)override{state->paused=value;}};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};Bytes header(g::original_save_header_bytes);put(header,0,2);put(header,4,2123104);
    for(auto [offset,value]:{std::pair<unsigned,unsigned>{8,2024},{10,2},{12,4},{14,29},{16,23},{18,58},{20,59},{22,999}})short_word(header,offset,value);
    const std::string description="OWNED\x81\x40";std::copy(description.begin(),description.end(),header.begin()+24);auto metadata=g::inspect_legacy_save_header(header);
    CHECK(metadata.version==2 && metadata.story_code==2123104 && metadata.date.year==2024 && metadata.date.month==2 && metadata.date.day==29 && metadata.date.weekday==4 && metadata.date.hour==23 && metadata.date.minute==58 && metadata.date.second==59 && metadata.millisecond==999 && metadata.description==description);++cases;
    {auto bytes=header;put(bytes,0,1);CHECK(g::inspect_legacy_save_header(bytes).version==1);bytes.insert(bytes.end(),100,0x91);CHECK(g::inspect_legacy_save_header(bytes).description==description);}++cases;
    for(unsigned count:{0,4,24,87}){rejects([&]{g::inspect_legacy_save_header(std::span(header).first(count));});++guards;}++cases;
    for(auto [at,value]:{std::pair<unsigned,unsigned>{0,0},{0,3},{4,10000000}}){auto bytes=header;put(bytes,at,value);rejects([&]{g::inspect_legacy_save_header(bytes);});++guards;}++cases;
    for(auto [at,value]:{std::pair<unsigned,unsigned>{8,0},{10,13},{12,7},{14,30},{16,24},{18,60},{20,60},{22,1000}}){auto bytes=header;short_word(bytes,at,value);rejects([&]{g::inspect_legacy_save_header(bytes);});++guards;}++cases;
    {auto bytes=header;std::fill(bytes.begin()+24,bytes.end(),65);rejects([&]{g::inspect_legacy_save_header(bytes);});++guards;}++cases;
    std::vector<std::shared_ptr<State>> songs,effects,voices;bool deny_effect{},deny_voice{};
    const auto music_factory=[&](int id,bool loop)->std::unique_ptr<a::Voice>{auto state=std::make_shared<State>();state->id=id;state->loop=loop;songs.push_back(state);return std::make_unique<Voice>(state);};
    const auto effect_factory=[&](int id,bool loop,bool paused)->std::unique_ptr<a::Voice>{if(deny_effect)throw std::runtime_error("owned effect provider failure");auto state=std::make_shared<State>();state->id=id;state->loop=loop;state->paused=paused;effects.push_back(state);return std::make_unique<Voice>(state);};
    const auto voice_factory=[&](const s::SpokenLine& line)->std::unique_ptr<a::Voice>{if(deny_voice)throw std::runtime_error("owned voice provider failure");auto state=std::make_shared<State>();state->speaker=line.speaker;state->chapter=line.chapter;state->line=line.line;state->loop=line.loop;voices.push_back(state);return std::make_unique<Voice>(state);};
    s::SceneMusic music(music_factory);s::SceneEffects se([&](auto id,auto loop){return effect_factory(id,loop,false);},{},effect_factory);s::SceneVoices speech(voice_factory,[]{return 1001;},[]{return 0;});
    const auto apply=[&](const g::LegacyAudioSave& data){music.restore(data.music);se.restore(data.effects);speech.restore(data.voices);};
    auto empty=scene();auto silence=g::import_legacy_scene_audio(empty);apply(silence);CHECK(music.song()==-1 && songs.empty() && effects.empty() && voices.empty());++cases;
    for(unsigned slot=0;slot<4;++slot)for(int phase:{1,3,5}){
        auto bytes=empty;put(bytes,0xefc+slot*4,17+slot);put(bytes,0xf0c+slot*4,unsigned(phase));put(bytes,0xf1c+slot*4,slot%2);put(bytes,0xf2c+slot*4,101+slot);apply(g::import_legacy_scene_audio(bytes));
        CHECK(music.song()==int(17+slot) && songs.back()->id==int(17+slot) && songs.back()->loop==(slot%2==0) && music.current().target==int(101+slot));music.update();CHECK(songs.back()->gain==101+slot);++cases;
    }
    for(int phase:{0,4}){auto bytes=empty;put(bytes,0xefc,9999);put(bytes,0xf0c,unsigned(phase));put(bytes,0xf2c,0xffffffff);const auto before=songs.size();apply(g::import_legacy_scene_audio(bytes));CHECK(music.song()==-1 && songs.size()==before);}++cases;
    auto active=empty;put(active,0xefc,31);put(active,0xf0c,1);put(active,0xf1c,0);put(active,0xf2c,203);
    for(unsigned n:{0u,15u}){put(active,0xf3c+n*4,1);put(active,0xf7c+n*4,9015+n);put(active,0xfbc+n*4,1);put(active,0xffc+n*4,97+n);}
    put(active,0xf3c+4,1);put(active,0xf7c+4,0xffffffff);put(active,0xfbc+4,0);put(active,0xffc+4,0xffffffff);
    put(active,0xf3c+8,0);put(active,0xf7c+8,0xffffffff);put(active,0xfbc+8,1);put(active,0xffc+8,0xffffffff);
    for(unsigned n:{0u,3u}){put(active,0x107c+n*4,1);put(active,0x108c+n*4,n?99:2);put(active,0x109c+n*4,2001+n);put(active,0x10ac+n*4,73+n);put(active,0x10bc+n*4,139+n);put(active,0x10cc+n*4,0xffffffff);}
    auto converted=g::import_legacy_scene_audio(active);apply(converted);se.update();music.update();CHECK(music.song()==31 && music.current().target==203 && se.slot(0).sound==9015 && se.slot(15).sound==9030 && !se.slot(1).voice && !se.slot(2).voice && speech.active()==2);
    CHECK(effects.size()==2 && effects[0]->loop && !effects[0]->paused && effects[0]->gain==97 && effects[1]->gain==112 && voices.size()==2 && voices[0]->speaker==2 && voices[0]->chapter==2001 && voices[0]->line==73 && voices[0]->loop && voices[0]->gain==139 && voices[1]->speaker==99 && voices[1]->chapter==2004 && voices[1]->line==76 && voices[1]->gain==142);++cases;
    const auto roundtrip=se.checkpoint();se.restore(roundtrip);CHECK(se.slot(0).loop && se.slot(15).loop && speech.checkpoint()==converted.voices);++cases;
    apply(silence);CHECK(!speech.active() && !se.slot(0).voice && !se.slot(15).voice && music.song()==-1 && voices[0]->stopped && voices[1]->stopped);++cases;
    auto one=empty;put(one,0x107c,1);put(one,0x108c,0);put(one,0x109c,1001);put(one,0x10ac,8);put(one,0x10bc,255);speech.settings(256,256,true);speech.adult_mode(true);const auto before=voices.size();speech.restore(g::import_legacy_scene_audio(one).voices);CHECK(voices.size()==before && !speech.active());speech.adult_mode(false);speech.restore(g::import_legacy_scene_audio(one).voices);CHECK(voices.size()==before+1 && speech.active()==1);++cases;
    {speech.enabled(false);const auto before=voices.size();speech.restore(converted.voices);CHECK(voices.size()==before && !speech.active());speech.enabled(true);}++cases;
    const auto reject=[&](const Bytes& bytes){const auto m=music.checkpoint(),e=se.checkpoint(),v=speech.checkpoint();const auto count=songs.size()+effects.size()+voices.size();rejects([&]{g::import_legacy_scene_audio(bytes);});CHECK(music.checkpoint()==m && se.checkpoint()==e && speech.checkpoint()==v && songs.size()+effects.size()+voices.size()==count);++guards;};
    reject({});reject(Bytes(g::original_scene_v2_bytes-1));reject(Bytes(g::original_scene_v2_bytes+1));++cases;
    for(auto [at,value]:{std::pair<unsigned,unsigned>{0xefc,200},{0xefc,0xfffffffe},{0xf0c,2},{0xf0c,6},{0xf2c,256},{0xf2c,0xffffffff},{0xf7c,0xffffffff},{0xffc,256},{0xffc,0xffffffff},{0x108c,100},{0x108c,0xffffffff},{0x109c,10000},{0x109c,0xffffffff},{0x10ac,10000},{0x10ac,0xffffffff},{0x10bc,256},{0x10bc,0xffffffff}}){auto bytes=active;put(bytes,at,value);reject(bytes);}++cases;
    {auto bytes=active;put(bytes,0xefc+4,32);put(bytes,0xf0c+4,1);put(bytes,0xf2c+4,255);reject(bytes);put(bytes,0xf0c+4,4);CHECK(g::import_legacy_scene_audio(bytes).music==converted.music);}++cases;
    {se.restore(converted.effects);const auto prior=se.checkpoint();deny_effect=true;rejects([&]{se.restore(converted.effects);});deny_effect=false;CHECK(se.checkpoint()==prior && se.slot(0).voice && !se.slot(0).voice->finished());++guards;
        speech.restore(converted.voices);const auto prior_voice=speech.checkpoint();deny_voice=true;rejects([&]{speech.restore(converted.voices);});deny_voice=false;CHECK(speech.checkpoint()==prior_voice && speech.active()==2);}++guards;++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-legacy-save.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"bounded_header_inspection\":true,\"source_timestamp_and_description\":true,\"source_audio_load_policy\":true,\"one_shots_omitted\":true,\"looping_channels_restored\":true,\"music_fade_out_omitted\":true,\"ambiguous_music_rejected\":true,\"existing_preferences_applied\":true,\"conversion_does_not_open_transports\":true,\"malformed_data_preserves_live_state\":true,\"manual_scene_import_complete\":false,\"fake_transports_only\":true,\"synthetic_data_only\":true,\"original_resources_opened\":false,\"original_saves_opened\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS original save header and audio records, constructed bytes and fake playback only.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
