#include "scene_voices.hpp"
#include <fstream>
#include <iostream>

namespace s=wa2::script;namespace a=wa2::audio;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
struct State {s::SpokenLine line;std::uint8_t gain{};bool done{},ready{true};std::string error;};
class Voice:public a::Voice {
    std::shared_ptr<State> state_;
public:
    explicit Voice(std::shared_ptr<State> state):state_(std::move(state)){}
    bool ready()const override{return state_->ready;}
    bool finished()const override{return state_->done;}
    std::string failure()const override{return state_->error;}
    void gain(std::uint8_t volume)override{state_->gain=volume;}
    void stop()override{state_->done=true;}
};
int main(int argc,char** argv){try{
    CHECK(argc==2);std::vector<std::shared_ptr<State>> created;bool skip{},advance{};unsigned cases=0;
    s::SceneVoices voices([&](const auto& line){auto state=std::make_shared<State>();state->line=line;created.push_back(state);return std::make_unique<Voice>(state);},
        []{return 1002;},[]{return 10;},[]{return 3;},[&]{return skip;},[&]{return advance;});
    std::array<s::Value,5> args{s::Value(-1),s::Value(256),s::Value(0),s::Value(0),s::Value(100)};s::Command call{138,args};voices.dispatch(call);
    CHECK(voices.active()==1 && created.back()->line.speaker==10 && created.back()->line.chapter==1002 && created.back()->line.line==103 && !created.back()->line.loop && created.back()->gain==255);++cases;
    voices.dispatch(call);CHECK(created.front()->done && voices.active()==1 && voices.played()==2);++cases;
    std::array<s::Value,2> wait{s::Value(0),s::Value(1)};call={140,wait};CHECK(!voices.dispatch(call)->complete);advance=true;CHECK(voices.dispatch(call)->complete);advance=false;++cases;
    std::array<s::Value,2> stop{s::Value(4),s::Value(0)};call={141,stop};voices.dispatch(call);voices.update();CHECK(created.back()->gain==191 && !created.back()->done);++cases;
    for(unsigned n=0;n<3;++n)voices.update();CHECK(created.back()->done && !voices.active());++cases;
    skip=true;call={138,args};voices.dispatch(call);CHECK(created.size()==2 && !voices.active());++cases;
    std::array<s::Value,6> forced{s::Value(2),s::Value(2003),s::Value(1),s::Value(128),s::Value(51),s::Value(1)};call={139,forced};voices.dispatch(call);
    CHECK(voices.active()==1 && created.back()->line.chapter==2003 && created.back()->line.speaker==2 && created.back()->line.line==54 && created.back()->line.loop);++cases;
    created.back()->done=true;voices.update();CHECK(!voices.active());++cases;
    forced[2]=s::Value(-1);call={139,forced};voices.dispatch(call);CHECK(created.back()->line.loop);++cases;
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-voices.v1\",\"cases\":"<<cases<<",\"maximum_voice_slots\":2,\"screen_access\":false,\"audio_devices_opened\":0,\"cpu_emulator_linked\":false,\"passed\":true}\n";
    std::cout<<"PASS native speech commands: "<<cases<<" cases.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
