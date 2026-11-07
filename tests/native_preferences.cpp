#include "preferences.hpp"
#include "scene_voices.hpp"
#include "checkpoint_codec.hpp"
#include <fstream>
#include <iostream>

namespace g=wa2::game;namespace s=wa2::script;namespace a=wa2::audio;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+ #x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
using Bytes=std::vector<std::uint8_t>;
struct Storage:g::PreferenceStorage {std::optional<Bytes> bytes;unsigned writes{};bool failing{};std::optional<Bytes> read()override{return bytes;}void replace(std::span<const std::uint8_t> b)override{if(failing)throw std::runtime_error("injected preference write failure");bytes=Bytes(b.begin(),b.end());++writes;}};
struct State {s::SpokenLine line;unsigned gain{},stops{};};
struct Voice:a::Voice {std::shared_ptr<State> state;explicit Voice(std::shared_ptr<State> v):state(std::move(v)){}bool ready()const override{return true;}bool finished()const override{return false;}std::string failure()const override{return {};}void gain(std::uint8_t v)override{state->gain=v;}void stop()override{++state->stops;}};
Bytes patch(Bytes bytes,unsigned at,unsigned value){for(unsigned n=0;n<4;++n)bytes.at(at+n)=std::uint8_t(value>>(8*n));const auto crc=wa2::checkpoint::crc(std::span(bytes).first(bytes.size()-4));for(unsigned n=0;n<4;++n)bytes[bytes.size()-4+n]=std::uint8_t(crc>>(8*n));return bytes;}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};auto memory=std::make_shared<Storage>();g::PreferenceStore settings(memory);g::Preferences defaults;CHECK(settings.current()==defaults && !memory->writes && !memory->bytes);++cases;
    CHECK(defaults.master==195 && defaults.music==134 && defaults.effects==195 && defaults.speech==195 && defaults.skin==0 && defaults.auto_ticks==137);++cases;
    auto value=defaults;value.master=256;value.music=0;value.skin=9;value.text_step=0;value.auto_ticks=36000;value.window=false;value.skip_read_only=false;value.mute_adult=true;value.voice_enabled=false;value.captions=false;value.character_voice[8]=false;CHECK(g::Preferences::decode(value.encode())==value);++cases;
    CHECK(settings.replace(value) && memory->writes==1 && settings.current()==value);CHECK(!settings.replace(value) && memory->writes==1);g::PreferenceStore reopened(memory);CHECK(reopened.current()==value);++cases;
    memory->failing=true;rejects([&]{settings.replace(defaults);});CHECK(settings.current()==value && g::Preferences::decode(*memory->bytes)==value && memory->writes==1);memory->failing=false;++guards;
    for(unsigned field=0;field<11;++field){const auto bytes=patch(defaults.encode(),12+field*4,0xffffffff);rejects([&]{g::Preferences::decode(bytes);});++guards;}
    for(unsigned field=0;field<15;++field){const auto bytes=patch(defaults.encode(),12+44+field*4,2);rejects([&]{g::Preferences::decode(bytes);});++guards;}
    auto bytes=value.encode();bytes.back()^=1;rejects([&]{g::Preferences::decode(bytes);});++guards;bytes=value.encode();bytes[4]=2;rejects([&]{g::Preferences::decode(bytes);});++guards;bytes=value.encode();bytes.pop_back();rejects([&]{g::Preferences::decode(bytes);});++guards;
    auto invalid=value;invalid.speech=257;rejects([&]{settings.replace(invalid);});CHECK(settings.current()==value && memory->writes==1);++guards;memory->bytes=bytes;rejects([&]{g::PreferenceStore corrupt(memory);});++guards;rejects([&]{g::PreferenceStore missing({});});++guards;
    std::vector<std::shared_ptr<State>> made;bool skip{};unsigned issued{};
    const auto factory=[&](const s::SpokenLine& line){auto state=std::make_shared<State>();state->line=line;made.push_back(state);return std::make_unique<Voice>(state);};
    s::SceneVoices voice(factory,[]{return 1001;},[]{return 0;},{},[&]{return skip;},{},{{},{},[&](auto,const auto&,auto){++issued;}});
    const auto play=[&](int speaker,int loop=1){std::array<s::Value,6> args{s::Value(speaker),s::Value(1001),s::Value(loop),s::Value(255),s::Value(1),s::Value(0)};s::Command command{139,args};voice.dispatch(command);};
    play(2);auto old=made.back();CHECK(voice.active()==1 && old->gain==255 && issued==1);++cases;
    std::array<bool,10> switches;switches.fill(true);switches[1]=false;voice.character_switches(switches);const auto count=made.size();play(2);CHECK(made.size()==count && !old->stops && voice.active()==1 && issued==2);++cases;
    skip=true;play(1,0);CHECK(made.size()==count && !old->stops);skip=false;++cases;
    voice.adult_mode(true);voice.settings(256,256,true);play(0);CHECK(made.size()==count && !old->stops);voice.adult_mode(false);++cases;
    switches[8]=switches[9]=false;voice.character_switches(switches);for(int id:{12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,98,99})play(id);CHECK(made.size()==count && !old->stops);++cases;
    play(38);CHECK(made.size()==count+1 && old->stops==1 && made.back()->line.speaker==38);old=made.back();++cases;
    voice.enabled(false);voice.update();CHECK(!old->stops && old->gain==0);play(38);CHECK(made.back()==old);voice.enabled(true);voice.update();CHECK(made.back()==old && old->gain==0);++cases;
    voice.settings(128,128);play(38);CHECK(made.back()->gain==63 && old->stops==1);++cases;
    auto saved=voice.checkpoint();voice.enabled(false);const auto before=made.size();voice.restore(saved);CHECK(made.size()==before && voice.active()==0);++cases;
    voice.enabled(true);switches[1]=true;voice.character_switches(switches);play(13);CHECK(made.size()==before);switches[8]=true;voice.character_switches(switches);play(13);CHECK(made.size()==before+1);saved=voice.checkpoint();switches[8]=false;voice.character_switches(switches);voice.restore(saved);CHECK(made.size()==before+1 && voice.active()==0);++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-preferences.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"memory_storage_only\":true,\"fake_voices_only\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS native preferences, failed-write isolation and original character voice switches.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
