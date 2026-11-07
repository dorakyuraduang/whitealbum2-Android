#include "stream_voice.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

namespace a=wa2::audio;namespace r=wa2::resource;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void until(F&& f){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!f()){CHECK(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
struct Gate {
    std::mutex mutex;std::condition_variable changed;bool entered{},released{};
    void wait(){std::unique_lock lock(mutex);entered=true;changed.notify_all();if(!changed.wait_for(lock,std::chrono::seconds(3),[&]{return released;}))throw r::Error("Synthetic worker gate timeout");}
    void await(){std::unique_lock lock(mutex);CHECK(changed.wait_for(lock,std::chrono::seconds(3),[&]{return entered;}));}
    void open(){std::lock_guard lock(mutex);released=true;changed.notify_all();}
};
struct Wave:r::Source {
    std::array<std::uint8_t,44> header{};unsigned bytes;bool fail{};
    const std::thread::id owner{std::this_thread::get_id()};mutable std::atomic<unsigned> reads{},caller_access{};
    explicit Wave(unsigned count=8):bytes(count){const auto put=[&](unsigned at,unsigned value){for(unsigned n=0;n<4;++n)header[at+n]=std::uint8_t(value>>(8*n));};
        std::memcpy(header.data(),"RIFF",4);put(4,36+bytes);std::memcpy(header.data()+8,"WAVEfmt ",8);put(16,16);header[20]=header[22]=1;put(24,1000);put(28,2000);header[32]=2;header[34]=16;std::memcpy(header.data()+36,"data",4);put(40,bytes);}
    void thread()const{if(owner==std::this_thread::get_id())++caller_access;}
    std::uint64_t size()const override{thread();return header.size()+bytes;}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{thread();++reads;if(fail)throw r::Error("Synthetic provider read failure");
        if(at>header.size()+bytes || out.size()>header.size()+bytes-at)throw r::Error("Synthetic voice read bounds");
        for(auto& byte:out){if(at<header.size())byte=header[at];else{const auto n=unsigned(at-header.size());byte=n%2?std::uint8_t((1+(n/2)%4)*16):0;}++at;}}
};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};a::Mixer mixer;auto source=std::make_shared<Wave>();Gate gate;
    auto voice=a::play_wave(mixer,source,false,[&]{gate.wait();},true);gate.await();
    CHECK(!voice->ready() && !voice->finished() && voice->failure().empty() && !source->reads && !source->caller_access);++cases;
    voice->gain(128);voice->pause(false);voice->pause(true);gate.open();until([&]{return voice->ready() || !voice->failure().empty();});CHECK(voice->ready() && voice->failure().empty());++cases;
    std::array<float,10> out;out.fill(1);mixer.render(out,1000);CHECK(out[0]==0 && out[8]==0 && !voice->finished());++cases;
    CHECK(voice->levels()==a::Levels{});voice->pause(false);const auto reads_before_meter=source->reads.load();CHECK((voice->levels()==a::Levels{8192,12288}) && source->reads==reads_before_meter);mixer.render(out,1000);CHECK(std::abs(out[0]-.125f*128/255)<.000001f && std::abs(out[6]-.5f*128/255)<.000001f && out[8]==0);until([&]{return voice->finished();});CHECK(voice->levels()==a::Levels{});++cases;
    mixer.render(out,1000);CHECK(out[0]==0 && out[6]==0);voice->stop();voice->stop();CHECK(voice->finished() && voice->failure().empty());++cases;
    CHECK(source->reads>0 && source->caller_access==0);++cases;
    {auto wave=std::make_shared<Wave>(8192);auto loop=a::play_wave(mixer,wave,true,{},true);loop->gain(255);until([&]{return loop->ready() || !loop->failure().empty();});CHECK(loop->ready() && loop->failure().empty());++cases;
        loop->pause(false);mixer.render(out,1000);CHECK(out[0]==.125f && out[6]==.5f && out[8]==.125f && !loop->finished());++cases;
        loop->stop();CHECK(loop->finished() && wave->caller_access==0);mixer.render(out,1000);CHECK(out[0]==0);++cases;}
    {Gate pending;auto wave=std::make_shared<Wave>();auto cancelled=a::play_wave(mixer,wave,false,[&]{pending.wait();});pending.await();cancelled->pause(true);cancelled->gain(255);pending.open();cancelled->stop();CHECK(cancelled->finished() && cancelled->failure().empty() && wave->caller_access==0);mixer.render(out,1000);CHECK(out[0]==0);++cases;}
    {auto wave=std::make_shared<Wave>();wave->fail=true;auto failed=a::play_wave(mixer,wave,false);until([&]{return !failed->failure().empty();});CHECK(failed->finished() && failed->failure()=="Synthetic provider read failure" && wave->caller_access==0);++cases;}
    {auto wave=std::make_shared<Wave>();auto failed=a::play_ogg(mixer,{wave,{}});until([&]{return !failed->failure().empty();});CHECK(failed->finished() && !failed->ready() && wave->caller_access==0);++cases;}
    {auto wave=std::make_shared<Wave>();auto failed=a::play_wave(mixer,wave,false,[]{throw r::Error("Synthetic thread initialization failure");});until([&]{return !failed->failure().empty();});CHECK(failed->finished() && failed->failure()=="Synthetic thread initialization failure" && !wave->reads);++cases;}
    bool missing=false;try{a::play_wave(mixer,{},false);}catch(const r::Error&){missing=true;}CHECK(missing);++guards;
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-stream-voice.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"caller_source_accesses\":0,\"synthetic_pcm_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS native asynchronous voice startup and cancellation: "<<cases<<" cases.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
