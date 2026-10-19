#include "scene_effects.hpp"
#include "wave_reader.hpp"
#include "file_io.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>
#include <iostream>
#include <fstream>
#include <vector>

namespace s=wa2::script;namespace a=wa2::audio;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
struct State{unsigned volume{},stops{};bool ready=true,done{},loop{},paused{};std::int32_t sound{};std::string error;};
class Voice final:public a::Voice{
    std::shared_ptr<State> state_;
public:
    explicit Voice(std::shared_ptr<State> state):state_(std::move(state)){}
    bool ready()const override{return state_->ready;}
    bool finished()const override{return state_->done;}
    std::string failure()const override{return state_->error;}
    void gain(std::uint8_t value)override{state_->volume=value;}
    void stop()override{++state_->stops;state_->done=true;}
    void pause(bool value)override{state_->paused=value;}
};
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}CHECK(rejected);}
struct Bytes final:wa2::resource::Source{
    std::vector<std::uint8_t> data;
    std::uint64_t size()const override{return data.size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{
        if(at>data.size() || out.size()>data.size()-at)throw std::runtime_error("test source bounds");
        if(!out.empty())std::memcpy(out.data(),data.data()+at,out.size());
    }
};
void put(std::vector<std::uint8_t>& bytes,std::size_t at,std::uint32_t value,unsigned count){
    for(unsigned n=0;n<count;++n)bytes.at(at+n)=std::uint8_t(value>>(8*n));
}
unsigned wave_tests(const std::filesystem::path& root){
    unsigned cases=0;
    for(unsigned bits:{8u,16u})for(unsigned channels:{1u,2u}){
        auto input=std::make_shared<Bytes>();const auto payload=8*channels*bits/8;input->data.resize(54+payload);
        auto& bytes=input->data;std::memcpy(bytes.data(),"RIFF",4);put(bytes,4,std::uint32_t(bytes.size()-8),4);std::memcpy(bytes.data()+8,"WAVEfmt ",8);
        put(bytes,16,16,4);put(bytes,20,1,2);put(bytes,22,channels,2);put(bytes,24,48000,4);
        put(bytes,28,48000*channels*bits/8,4);put(bytes,32,channels*bits/8,2);put(bytes,34,bits,2);
        std::memcpy(bytes.data()+36,"JUNK",4);put(bytes,40,1,4);bytes[44]=42;
        std::memcpy(bytes.data()+46,"data",4);put(bytes,50,payload,4);
        for(unsigned n=0;n<payload;++n)bytes[54+n]=std::uint8_t(n*31);
        a::WaveReader reader(input);CHECK(reader.format().rate==48000 && reader.format().channels==channels && reader.frames()==8);++cases;
        std::vector<std::uint8_t> output(16*channels);CHECK(reader.read(output)==output.size() && reader.finished());++cases;
        for(unsigned n=0;n<8*channels;++n){const auto expected=bits==8?std::uint16_t((int(bytes[54+n])-128)*256):
            std::uint16_t(unsigned(bytes[54+n*2])|(unsigned(bytes[55+n*2])<<8));
            CHECK(output[n*2]==std::uint8_t(expected) && output[n*2+1]==std::uint8_t(expected>>8));}
        CHECK(reader.read(output)==0);reader.rewind();CHECK(reader.read(output)==output.size());++cases;
        rejects([&]{reader.read(std::span(output).first(1));});++cases;
        put(bytes,20,17,2);rejects([&]{a::WaveReader unsupported(input);});++cases;
        put(bytes,20,1,2);bytes.pop_back();rejects([&]{a::WaveReader truncated(input);});++cases;
    }
    auto archive=wa2::resource::Archive::open(wa2::platform::open_resource_file(root/"IC/SE.PAK"));
    const auto member=archive.find_first("SE_8160.WAV");CHECK(member!=wa2::resource::Archive::missing);
    auto source=archive.open_member(member);a::WaveReader reader(source);CHECK(reader.format().channels==1 && reader.format().rate==48000);++cases;
    std::array<std::uint8_t,2048> pcm{},expected{};CHECK(reader.read(pcm)==pcm.size());source->read_at(44,expected);CHECK(pcm==expected);++cases;
    a::Mixer mixer;auto voice=a::play_wave(mixer,source,true);voice->gain(255);
    for(unsigned n=0;n<200 && !voice->ready();++n){CHECK(voice->failure().empty());std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    CHECK(voice->ready());std::array<float,4> output{};mixer.render(output,48000);
    const auto sample=std::int16_t(unsigned(pcm[0])|(unsigned(pcm[1])<<8));CHECK(std::abs(output[0]-sample/32768.f)<0.000001f && output[0]==output[1]);++cases;
    voice->stop();CHECK(voice->finished());++cases;
    auto prepared=a::play_wave(mixer,source,false,{},true);prepared->gain(255);
    for(unsigned n=0;n<200 && !prepared->ready();++n)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(prepared->ready());std::array<float,2048> silent{};mixer.render(silent,48000);CHECK(std::all_of(silent.begin(),silent.end(),[](auto value){return value==0;}));++cases;
    prepared->pause(false);mixer.render(output,48000);CHECK(std::abs(output[0]-sample/32768.f)<.000001f);prepared->stop();++cases;return cases;
}
int main(int argc,char** argv){try{
    CHECK(argc==3);const auto wave_cases=wave_tests(argv[1]);unsigned cases=0;bool skip=false;std::vector<std::shared_ptr<State>> voices;
    s::SceneEffects effects([&](auto sound,bool loop){auto state=std::make_shared<State>();state->sound=sound;state->loop=loop;
        voices.push_back(state);return std::make_unique<Voice>(state);},[&]{return skip;});
    effects.play(0,10,3,1,240);CHECK(voices.size()==1 && voices[0]->loop && voices[0]->volume==0);++cases;
    for(unsigned n=0;n<4;++n){effects.update();CHECK(voices[0]->volume==n*80);++cases;}
    effects.settings(128,128);CHECK(voices[0]->volume==60);effects.settings(256,256);++cases;
    effects.volume(0,100,2);effects.update();CHECK(voices[0]->volume==240);effects.update();CHECK(voices[0]->volume==170);
    effects.update();CHECK(voices[0]->volume==100);++cases;
    effects.stop(0,2);effects.update();effects.update();CHECK(voices[0]->volume==50 && !voices[0]->done);
    effects.update();CHECK(voices[0]->done && !effects.slot(0).voice);++cases;
    effects.play(1,11,0,0,200);CHECK(voices.back()->volume==200 && !voices.back()->loop);++cases;
    auto previous=voices.back();effects.play(1,12,0,0,190);CHECK(previous->done && voices.back()->volume==190);++cases;
    voices.back()->ready=false;effects.volume(1,90,2);effects.update();CHECK(effects.slot(1).elapsed==0);voices.back()->ready=true;++cases;
    effects.update([](auto){return 0;});CHECK(voices.back()->volume==90);++cases;
    voices.back()->error="test decoder failure";rejects([&]{effects.update();});voices.back()->error.clear();++cases;
    const std::array<s::Value,2> wait_args{s::Value(1),s::Value(1)};s::Command wait{168,wait_args};
    CHECK(!effects.dispatch(wait)->complete);voices.back()->done=true;CHECK(!effects.dispatch(wait)->complete);
    effects.update();CHECK(effects.dispatch(wait)->complete);++cases;
    effects.play(1,13,0,0,200);const std::array<s::Value,2> skip_args{s::Value(1),s::Value(0)};
    wait={168,skip_args};skip=true;CHECK(effects.dispatch(wait)->complete && voices.back()->done);++cases;
    const auto before=voices.size();for(unsigned n=0;n<9;++n)effects.once(20+int(n),200);CHECK(voices.size()==before+8);++cases;
    voices[before+3]->done=true;effects.update();effects.once(50,255);CHECK(voices.size()==before+9 && voices.back()->sound==50);++cases;
    rejects([&]{effects.play(16,1,0,0,255);});rejects([&]{effects.play(-1,1,0,0,255);});
    rejects([&]{effects.play(0,1,0,0,255,1);});++cases;
    effects.stop_all();for(const auto& voice:voices)CHECK(voice->done);++cases;
    std::shared_ptr<State> prepared_state;
    s::SceneEffects prepared_effects([](auto,bool)->std::unique_ptr<a::Voice>{throw std::runtime_error("ordinary factory used for preparation");},{},
        [&](auto sound,bool loop,bool paused){prepared_state=std::make_shared<State>();prepared_state->sound=sound;prepared_state->loop=loop;prepared_state->paused=paused;return std::make_unique<Voice>(prepared_state);});
    prepared_effects.play(2,111,0,1,256,1);prepared_effects.update();CHECK(prepared_state->paused && prepared_effects.slot(2).prepared);++cases;
    auto old_prepared=prepared_state;prepared_effects.play(2,999,2,1,200,2);CHECK(old_prepared==prepared_state && !prepared_state->paused && !prepared_effects.slot(2).prepared);++cases;
    for(int n=0;n<3;++n)prepared_effects.update();CHECK(prepared_state->volume==200);prepared_effects.stop_all();CHECK(prepared_state->done);++cases;
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.native-effects.v1\",\"cases\":"<<cases<<",\"wave_cases\":"<<wave_cases
        <<",\"channel_limit\":16,\"ordinary_pool_limit\":8,\"prepared_modes_implemented\":false,"
        "\"screen_access\":false,\"audio_device_opened\":false,\"game_launched\":false,\"passed\":true}\n";
    std::cout<<"PASS native sound effects: "<<cases<<" checks\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
