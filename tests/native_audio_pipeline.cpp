#include "ogg_reader.hpp"
#include "pcm_stream.hpp"
#include "pcm_audio.hpp"
#include "stream_voice.hpp"
#include "scene_music.hpp"
#include "scene_controls.hpp"
#include "script_session.hpp"
#include "file_io.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

namespace a=wa2::audio;
namespace s=wa2::script;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
unsigned stream_cases{},music_cases{},ogg_cases{};
void near(float x,float y){CHECK(std::abs(x-y)<0.00001f);}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
std::vector<std::uint8_t> samples(unsigned frames,unsigned channels){
    std::vector<std::uint8_t> result(frames*channels*2);
    for(unsigned n=0;n<frames;++n)for(unsigned c=0;c<channels;++c){
        auto word=std::bit_cast<std::uint16_t>(std::int16_t(int((n*317+c*271)%58000)-29000));const auto at=(n*channels+c)*2;
        result[at]=std::uint8_t(word);result[at+1]=std::uint8_t(word>>8);
    }
    return result;
}
void stream_tests(){
    {
        a::Mixer mixer;auto stream=mixer.create_stream(48000,2,16);const auto data=samples(8,2);stream->write(data);stream->play();
        std::array<float,4> output{};mixer.render(output,48000);const auto before=stream->status();stream->pause();
        mixer.render(output,48000);CHECK(output[0]==0 && output[1]==0 && stream->status().consumed_frames==before.consumed_frames);
        CHECK(stream->status().queued_frames==before.queued_frames && !stream->status().finished);
        stream->play();mixer.render(output,48000);CHECK(stream->status().consumed_frames==before.consumed_frames+2);++stream_cases;
    }
    {
        a::Mixer mixer;auto stream=mixer.create_stream(48000,1,2);const auto bytes=samples(5,1);
        CHECK(stream->write(bytes)==4 && stream->status().free_frames==0);
        CHECK(stream->write(bytes)==0);std::array<float,8> out{};mixer.render(out,48000);
        CHECK(std::all_of(out.begin(),out.end(),[](float x){return x==0;}));
        stream->play();mixer.render(out,48000);near(out[0],-29000/32768.f);near(out[2],-28683/32768.f);
        CHECK(out[4]==0 && out[6]==0 && stream->status().underruns==1 && stream->status().queued_frames==0);
        mixer.render(out,48000);CHECK(std::all_of(out.begin(),out.end(),[](float x){return x==0;}));
        CHECK(stream->write(std::span(bytes).subspan(4,2))==2);stream->finish_input();mixer.render(out,48000);
        near(out[0],-28366/32768.f);CHECK(stream->status().finished && stream->status().consumed_frames==3);
        rejects([&]{stream->write(bytes);});++stream_cases;
    }
    for(const unsigned rate:{8000u,22050u,44100u,48000u})for(const unsigned output_rate:{8000u,44100u,48000u})for(unsigned channels:{1u,2u}){
        const auto data=samples(253,channels);a::Mixer reference,actual;
        auto buffer=reference.create({rate,std::uint16_t(channels),a::Encoding::signed16},std::uint32_t(data.size()));
        buffer->edit(0,std::uint32_t(data.size()),[&](auto destination){std::copy(data.begin(),data.end(),destination.begin());});buffer->play(false);
        auto stream=actual.create_stream(rate,channels,128);stream->play();std::size_t cursor{};bool end{};
        for(unsigned n=0;n<(253*output_rate+rate-1)/rate+16;n+=8){
            if(cursor<data.size())cursor+=stream->write(std::span(data).subspan(cursor));
            if(cursor==data.size() && !end){stream->finish_input();end=true;}
            std::array<float,16> wanted{},got{};reference.render(wanted,output_rate);actual.render(got,output_rate);
            for(unsigned i=0;i<got.size();++i)near(got[i],wanted[i]);
        }
        CHECK(stream->status().finished && stream->status().consumed_frames==253);++stream_cases;
    }
    {
        a::Mixer mixer;auto stream=mixer.create_stream(48000,2,8);auto data=samples(8,2);stream->write(data);stream->gain(.25f);stream->play();
        std::array<float,2> out{};mixer.render(out,48000);near(out[0],-29000/32768.f*.25f);
        stream->stop();mixer.render(out,48000);CHECK(out[0]==0 && out[1]==0 && stream->status().finished);
        rejects([&]{stream->gain(std::numeric_limits<float>::quiet_NaN());});
        rejects([&]{stream->gain(-1);});rejects([&]{stream->write(std::span(data).first(3));});
        rejects([&]{mixer.create_stream(0,2);});rejects([&]{mixer.create_stream(48000,8);});++stream_cases;
    }
}
struct VoiceState {bool ready{true},ended{},stopped{};std::string error;std::uint8_t gain{};};
class TestVoice final:public a::Voice {
    std::shared_ptr<VoiceState> state_;
public:
    explicit TestVoice(std::shared_ptr<VoiceState> state):state_(std::move(state)){}
    bool ready()const override{return state_->ready;}
    bool finished()const override{return state_->ended;}
    std::string failure()const override{return state_->error;}
    void gain(std::uint8_t value)override{state_->gain=value;}
    void stop()override{state_->stopped=true;}
};
void music_tests(){
    unsigned loads=0,seen=0;std::vector<std::shared_ptr<VoiceState>> voices;
    s::SceneMusic music([&](std::int32_t song,bool loop){CHECK(song>=0 && loop);++loads;auto state=std::make_shared<VoiceState>();
        voices.push_back(state);return std::make_unique<TestVoice>(state);},[&](std::int32_t){++seen;});
    music.stop(5);CHECK(music.current().phase==s::MusicPhase::idle);++music_cases;
    music.play(1,4,0,200);CHECK(loads==1 && seen==1 && voices[0]->gain==0);++music_cases;
    for(unsigned tick=0;tick<=4;++tick){music.update();CHECK(music.current().volume==int(tick*50));++music_cases;}
    CHECK(music.current().phase==s::MusicPhase::playing);
    music.play(1,2,0,100);CHECK(loads==1 && music.current().phase==s::MusicPhase::volume);++music_cases;
    music.update();CHECK(voices[0]->gain==200);music.update();CHECK(voices[0]->gain==150);
    music.update();CHECK(voices[0]->gain==100 && music.current().phase==s::MusicPhase::playing);++music_cases;
    music.settings(128,128);CHECK(voices[0]->gain==25);music.settings(256,256);++music_cases;
    music.play(2,2,0,240);CHECK(loads==2 && music.slots()[0].phase==s::MusicPhase::fade_in &&
        music.slots()[1].phase==s::MusicPhase::fade_out);++music_cases;
    music.update();CHECK(voices[0]->gain==100 && voices[1]->gain==0);
    music.update();CHECK(voices[0]->gain==50 && voices[1]->gain==120);
    music.update();CHECK(voices[0]->stopped && voices[1]->gain==240);++music_cases;
    music.stop(1);CHECK(music.song()==-1 && !voices[1]->stopped);music.update();CHECK(!voices[1]->stopped);
    music.update();CHECK(voices[1]->stopped && music.current().phase==s::MusicPhase::idle);++music_cases;
    music.play(3,60,0,300);music.update([](auto){return 0;});CHECK(voices[2]->gain==255 && music.current().phase==s::MusicPhase::playing);++music_cases;
    voices[2]->error="decoder failure";rejects([&]{music.update();});voices[2]->error.clear();++music_cases;
    const std::array<s::Value,1> stop{s::Value(0)};s::Command command{159,stop};
    CHECK(music.dispatch(command)->complete && !music.dispatch(command)->yield_frame);++music_cases;
    s::Command wait{163,{}};CHECK(!music.dispatch(wait)->complete);music.update([](auto){return 0;});CHECK(music.dispatch(wait)->complete);++music_cases;
}
struct CountingSource final:wa2::resource::Source{
    std::shared_ptr<wa2::resource::Source> source;std::atomic<std::uint64_t> bytes{};std::atomic<std::size_t> largest{};
    explicit CountingSource(std::shared_ptr<wa2::resource::Source> value):source(std::move(value)){}
    std::uint64_t size()const override{return source->size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{
        source->read_at(at,out);bytes+=out.size();auto previous=largest.load();
        while(previous<out.size() && !largest.compare_exchange_weak(previous,out.size())){}
    }
};
int main(int argc,char** argv){try{
    CHECK(argc==3);stream_tests();music_tests();const auto root=std::filesystem::path(argv[1]);
    auto raw=std::make_shared<CountingSource>(wa2::platform::open_resource_file(root/"BGM.PAK"));
    const auto bank=wa2::resource::Archive::open(raw);const auto initial_bytes=raw->bytes.load();
    auto playlist=a::music_playlist(bank,1,true);CHECK(playlist.first && playlist.repeat && playlist.first!=playlist.repeat);
    CHECK(raw->bytes==initial_bytes);++ogg_cases;
    auto single=a::music_playlist(bank,4,true);CHECK(single.first==single.repeat);++ogg_cases;
    auto once=a::music_playlist(bank,1,false);CHECK(!once.repeat);rejects([&]{a::music_playlist(bank,999,true);});++ogg_cases;
    a::OggReader reader(playlist.first);const auto format=reader.format();std::vector<std::uint8_t> prefix(4096*format.channels*2);
    CHECK(reader.read(prefix)==prefix.size());CHECK(reader.buffered_bytes()<5*1024*1024);reader.rewind();
    std::vector<std::uint8_t> fragmented(prefix.size());std::size_t cursor=0;
    while(cursor<fragmented.size()){const auto size=std::min<std::size_t>(fragmented.size()-cursor,127*format.channels*2);
        CHECK(reader.read(std::span(fragmented).subspan(cursor,size))==size);cursor+=size;}
    CHECK(prefix==fragmented);++ogg_cases;
    {
        a::Mixer mixer;auto voice=a::play_ogg(mixer,{playlist.first,{}});voice->gain(255);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!voice->ready() && voice->failure().empty() && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        CHECK(voice->ready() && voice->failure().empty());
        // Only 4096 source frames, with no device or real-time playback.
        std::vector<float> actual(4096*2);mixer.render(actual,format.rate);
        for(unsigned n=0;n<4096;++n)for(unsigned c=0;c<2;++c){const auto at=(n*format.channels+std::min(c,format.channels-1))*2;
            const auto value=std::bit_cast<std::int16_t>(std::uint16_t(unsigned(prefix[at])|(unsigned(prefix[at+1])<<8)));
            near(actual[n*2+c],value/32768.f);}
        voice->stop();CHECK(voice->finished());++ogg_cases;
    }
    CHECK(raw->bytes<512*1024 && raw->largest<=8192);++ogg_cases;
    std::uint32_t next_command=0;std::string next_name;std::vector<s::Value> next_arguments;
    {
        s::Library library(wa2::resource::Archive::open(wa2::platform::open_resource_file(root/"ck-gal.pak")));
        a::Mixer mixer;s::SceneMusic music([&](auto id,bool loop){return a::play_ogg(mixer,a::music_playlist(bank,id,loop));});
        s::SceneControls controls([]{return 100u;});s::Session session(library);session.start("1001");
        CHECK(session.advance({})==s::RunState::yielded);
        CHECK(session.advance([&](s::Command& call){
            if(auto result=controls.dispatch(call))return *result;
            if(auto result=music.dispatch(call))return *result;
            next_command=call.index;next_name=library.functions().at(call.index).name;
            next_arguments.assign(call.arguments.begin(),call.arguments.end());return s::CommandResult{false,true,{}};
        })==s::RunState::yielded);
        CHECK(next_command && next_command!=159 && music.song()==-1);++music_cases;
    }
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.native-audio-pipeline.v1\",\"stream_cases\":"<<stream_cases
        <<",\"music_cases\":"<<music_cases<<",\"ogg_cases\":"<<ogg_cases<<",\"source_bytes_read\":"<<raw->bytes.load()
        <<",\"largest_audio_read\":"<<raw->largest.load()<<",\"next_unimplemented_command\":"<<next_command
        <<",\"next_unimplemented_command_name\":\""<<next_name<<"\",\"cpu_emulator_linked\":false,\"audio_device_opened\":false,"
        "\"game_launched\":false,\"screen_access\":false,\"whole_archive_extraction\":false,\"whole_track_pcm_decoded\":false,"
        "\"original_volume_curve_verified\":false,\"complete_gameplay_verified\":false,\"apk_created\":false,\"passed\":true}\n";
    std::cout<<"PASS native audio: "<<stream_cases<<" stream, "<<music_cases<<" music, "<<ogg_cases<<" Ogg checks; next real scene command "<<next_command<<" "<<next_name;
    for(const auto& value:next_arguments)std::cout<<' '<<(value.type()==s::Type::text?value.text():std::to_string(value.integer()));
    std::cout<<"\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
