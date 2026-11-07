#include "pcm_stream.hpp"
#include "music_caption.hpp"
#include <bit>
#include <fstream>
#include <iostream>

namespace a=wa2::audio;namespace g=wa2::game;namespace s=wa2::script;namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
using Bytes=std::vector<std::uint8_t>;
Bytes pcm(unsigned pairs,std::int16_t left,std::int16_t right){Bytes result;for(unsigned n=0;n<pairs;++n)for(auto value:{left,right}){const auto raw=std::bit_cast<std::uint16_t>(value);result.push_back(std::uint8_t(raw));result.push_back(std::uint8_t(raw>>8));}return result;}
struct VoiceState {a::Levels levels{};bool ready{true},done{},paused{};};
struct Voice:a::Voice {
    std::shared_ptr<VoiceState> state;explicit Voice(std::shared_ptr<VoiceState> value):state(std::move(value)){}
    bool ready()const override{return state->ready;}bool finished()const override{return state->done;}std::string failure()const override{return {};}
    void gain(std::uint8_t)override{}void stop()override{state->done=true;}void pause(bool value)override{state->paused=value;}
    a::Levels levels()const override{return state->paused?a::Levels{}:state->levels;}
};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};a::Mixer mixer;
    auto stream=mixer.create_stream(48000,2,512);auto bytes=pcm(256,-32768,16384);const auto tail=pcm(44,1,-1);bytes.insert(bytes.end(),tail.begin(),tail.end());CHECK(stream->write(bytes)==bytes.size() && stream->levels()==a::Levels{});++cases;
    stream->play();CHECK((stream->levels()==a::Levels{32768,16384}));const auto position=stream->status();for(unsigned n=0;n<3;++n)CHECK((stream->levels()==a::Levels{32768,16384}));CHECK(stream->status().consumed_frames==position.consumed_frames && stream->status().queued_frames==position.queued_frames);++cases;
    stream->gain(0);CHECK((stream->levels()==a::Levels{32768,16384}));std::array<float,128> output;mixer.render(output,48000);CHECK(std::all_of(output.begin(),output.end(),[](float value){return value==0;}));CHECK((stream->levels()==a::Levels{(192u*32768+44)/236,(192u*16384+44)/236}));++cases;
    stream->pause();CHECK(stream->levels()==a::Levels{});stream->play();CHECK(stream->levels()[0]>0);stream->stop();CHECK(stream->levels()==a::Levels{});++cases;
    auto wrapped=mixer.create_stream(48000,2,8);CHECK(wrapped->write(pcm(6,1000,-2000))==24);wrapped->play();std::array<float,8> four;mixer.render(four,48000);CHECK(wrapped->status().consumed_frames==4 && wrapped->write(pcm(6,-3000,4000))==24);CHECK((wrapped->levels()==a::Levels{2500,3500}));++cases;
    wrapped->finish_input();std::array<float,16> eight;mixer.render(eight,48000);CHECK(wrapped->status().finished && wrapped->levels()==a::Levels{});++cases;
    auto mono=mixer.create_stream(48000,1,600);CHECK(mono->write(pcm(256,-100,500))==1024);mono->play();CHECK((mono->levels()==a::Levels{100,500}));std::array<float,2> one;mixer.render(one,48000);CHECK((mono->levels()==a::Levels{500,100}));mono->stop();++cases;
    auto odd=mixer.create_stream(48000,1,2);const std::array<std::uint8_t,2> single{255,127};CHECK(odd->write(single)==2);odd->play();CHECK(odd->levels()==a::Levels{});odd->stop();++cases;
    CHECK((a::scaled_levels({301,701},255,128,128)==a::Levels{74,174}));++cases;
    std::vector<std::shared_ptr<VoiceState>> voices;const auto factory=[&](auto,bool)->std::unique_ptr<a::Voice>{auto state=std::make_shared<VoiceState>();voices.push_back(state);return std::make_unique<Voice>(state);};
    s::SceneMusic music(factory);s::SceneEffects effects(factory);music.play(31,0,0,255);auto bgm=voices.back();bgm->levels={300,700};CHECK((music.levels()==a::Levels{298,697}));CHECK(g::music_caption_state(music,effects,false).song==31 && (g::music_caption_state(music,effects,false).levels==a::Levels{298,697}));++cases;
    bgm->ready=false;CHECK(music.levels()==a::Levels{});bgm->ready=true;bgm->done=true;CHECK(music.levels()==a::Levels{});bgm->done=false;CHECK(g::music_caption_state(music,effects,true).levels==a::Levels{});++cases;
    effects.play(0,1,0,0,255);auto first=voices.back();first->levels={100,200};effects.play(1,2,0,0,255);auto second=voices.back();second->levels={50,500};CHECK((effects.levels()==a::Levels{49,498}) && (g::music_caption_state(music,effects,false).levels==a::Levels{298,697}));++cases;
    music.stop(0);CHECK(g::music_caption_state(music,effects,false).song==-1 && (g::music_caption_state(music,effects,false).levels==a::Levels{49,498}));second->done=true;CHECK((effects.levels()==a::Levels{99,199}));second->done=false;second->ready=false;CHECK((effects.levels()==a::Levels{99,199}));++cases;
    effects.settings(0,256);CHECK(effects.levels()==a::Levels{});effects.settings(256,0);CHECK(effects.levels()==a::Levels{});effects.settings(256,256);second->ready=true;effects.stop_all();CHECK(effects.levels()==a::Levels{});++cases;
    auto marker=std::make_shared<i::Raster>();marker->width=192;marker->height=8;marker->rgba.resize(192*8*4);auto labels=std::make_shared<i::Raster>();labels->width=264;labels->height=1008;labels->rgba.resize(264*1008*4);std::vector<i::Sprite> sprites;int order=0x43f;
    g::append_music_caption(sprites,marker,labels,{31,{300,700}},0x1063,order);CHECK(sprites.size()==2 && sprites[0].x==24 && sprites[0].y==688 && sprites[0].width==20 && sprites[0].source_width==20 && sprites[0].order==0x43f && sprites[1].source_y==32*16 && sprites[1].order==0x440 && order==0x441);++cases;
    sprites.clear();order=0x43f;g::append_music_caption(sprites,marker,labels,{1,{}},0x1063,order);CHECK(sprites.size()==1 && sprites[0].image==labels && sprites[0].order==0x440 && sprites[0].source_y==16);++cases;
    sprites.clear();order=0;g::append_music_caption(sprites,marker,labels,{-1,{199,0}},0x1063,order);CHECK(sprites.empty());g::append_music_caption(sprites,marker,labels,{-1,{200,0}},0x1063,order);CHECK(sprites.size()==1 && sprites[0].image==marker && sprites[0].width==4);++cases;
    sprites.clear();g::append_music_caption(sprites,marker,labels,{35,{32768,32768}},0x1063,order);CHECK(sprites.size()==1 && sprites[0].width==192 && sprites[0].source_width==192);++cases;
    for(auto state:{g::MusicCaption{-2},g::MusicCaption{200},g::MusicCaption{1,{32769,0}},g::MusicCaption{1,{0,32769}}}){rejects([&]{g::append_music_caption(sprites,marker,labels,state,0x1063,order);});++guards;}
    rejects([&]{g::append_music_caption(sprites,{},labels,1,0x1063,order);});++guards;marker->width=191;rejects([&]{g::append_music_caption(sprites,marker,labels,1,0x1063,order);});++guards;
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-audio-levels.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"source_window_bytes\":1024,\"raw_mean_not_output_gain\":true,\"source_meter_gain_stages\":true,\"reading_does_not_consume_pcm\":true,\"stereo_and_mono_pairing\":true,\"queue_wrap_and_finished\":true,\"music_or_loudest_effect\":true,\"caption_rows_and_meter_steps\":true,\"owned_pcm_only\":true,\"fake_voices_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(report);std::cout<<"PASS native bounded PCM levels, source meter policy and small captions.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
