#include "system_sounds.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>

namespace a=wa2::audio;namespace r=wa2::resource;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
struct Wave:r::Source {
    std::array<std::uint8_t,44> header{};std::size_t reads{};unsigned data_bytes{};
    explicit Wave(unsigned count=8,unsigned rate=1000):data_bytes(count){
        const auto put=[&](unsigned at,unsigned v){for(unsigned n=0;n<4;++n)header[at+n]=std::uint8_t(v>>(n*8));};
        std::memcpy(header.data(),"RIFF",4);put(4,36+count);std::memcpy(header.data()+8,"WAVEfmt ",8);put(16,16);
        header[20]=header[22]=1;put(24,rate);put(28,rate*2);header[32]=2;header[34]=16;std::memcpy(header.data()+36,"data",4);put(40,count);
    }
    std::uint64_t size()const override{return header.size()+data_bytes;}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{
        ++reads;if(at>size() || out.size()>size()-at)throw r::Error("System sound fixture bounds");
        for(auto& byte:out){if(at<header.size())byte=header[at];else{const auto n=unsigned(at-header.size());byte=n%2?std::uint8_t((1+(n/2)%4)*16):0;}++at;}
    }
};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},loads{};a::Mixer mixer;std::array<std::shared_ptr<Wave>,5> sources;for(auto& source:sources)source=std::make_shared<Wave>();
    constexpr std::array<int,5> ids{9211,9212,9213,9214,9015};
    a::SystemSounds sounds(mixer,[&](auto id){CHECK(loads<ids.size() && id==ids[loads]);return a::SoundResource{sources[loads++],false};});
    CHECK(!sounds.ready() && sounds.pcm_bytes()==0);rejects([&]{sounds.play(a::SystemCue::focus);});++guards;++cases;
    sounds.prepare();CHECK(sounds.ready() && sounds.pcm_bytes()==40 && loads==5 && mixer.live_buffers()==5);sounds.prepare();CHECK(loads==5);++cases;
    std::array<std::size_t,5> reads;for(unsigned n=0;n<5;++n)reads[n]=sources[n]->reads;
    std::array<float,4> out{};sounds.play(a::SystemCue::focus);mixer.render(out,1000);CHECK(out[0]==.125f && out[1]==.125f && out[2]==.25f);++cases;
    sounds.play(a::SystemCue::accept);mixer.render(out,1000);CHECK(out[0]==.5f && out[2]==.75f);++cases;
    sounds.stop();sounds.play(a::SystemCue::focus);mixer.render(out,1000);sounds.play(a::SystemCue::focus);mixer.render(out,1000);CHECK(out[0]==.125f && out[2]==.25f && mixer.live_buffers()==5);++cases;
    sounds.stop();sounds.play(a::SystemCue::cancel);sounds.play(a::SystemCue::reject);sounds.play(a::SystemCue::message);mixer.render(out,1000);CHECK(out[0]==.375f && out[2]==.75f);++cases;
    sounds.settings(256,256,false);mixer.render(out,1000);CHECK(out[0]==0);sounds.play(a::SystemCue::focus);mixer.render(out,1000);CHECK(out[0]==0);++cases;
    sounds.settings(128,256);sounds.play(a::SystemCue::focus);mixer.render(out,1000);CHECK(std::abs(out[0]-.125f*std::pow(10.f,-598.f/2000.f))<.00001f);++cases;
    sounds.settings(0,256);sounds.play(a::SystemCue::message);mixer.render(out,1000);CHECK(out[0]==0);sounds.settings(256,0);sounds.play(a::SystemCue::accept);mixer.render(out,1000);CHECK(out[0]==0);++cases;
    sounds.settings(256,256);sounds.play(a::SystemCue::message);std::array<float,10> tail{};mixer.render(tail,1000);CHECK(tail[0]==.125f && tail[6]==.5f && tail[8]==0);mixer.render(out,1000);CHECK(out[0]==0);++cases;
    for(unsigned n=0;n<5;++n)CHECK(sources[n]->reads==reads[n]);CHECK(loads==5);++cases;
    rejects([&]{sounds.settings(-1,256);});rejects([&]{sounds.settings(257,256);});rejects([&]{sounds.settings(256,-1);});rejects([&]{sounds.settings(256,257);});rejects([&]{sounds.play(a::SystemCue(5));});guards+=5;
    rejects([&]{a::SystemSounds invalid(mixer,{});});++guards;
    {a::Mixer local;unsigned call{};a::SystemSounds retry(local,[&](auto){return a::SoundResource{++call==3?nullptr:sources[0],false};});
        rejects([&]{retry.prepare();});CHECK(!retry.ready() && !retry.pcm_bytes() && local.live_buffers()==0);++guards;retry.prepare();CHECK(retry.ready() && local.live_buffers()==5);++cases;}
    for(unsigned bytes:{0u,4u*1024*1024+2}){a::Mixer local;a::SystemSounds bad(local,[&](auto){return a::SoundResource{std::make_shared<Wave>(bytes),false};});rejects([&]{bad.prepare();});CHECK(!bad.ready() && local.live_buffers()==0);++guards;}++cases;
    {a::Mixer local;a::SystemSounds ogg(local,[&](auto){return a::SoundResource{sources[0],true};});rejects([&]{ogg.prepare();});CHECK(!ogg.ready());++guards;++cases;}
    {a::Mixer local;a::SystemSounds rate(local,[&](auto){return a::SoundResource{std::make_shared<Wave>(8,300000),false};});rejects([&]{rate.prepare();});CHECK(!rate.ready());++guards;++cases;}
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-system-sounds.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"cue_buffers\":5,\"playback_resource_reads\":0,\"synthetic_pcm_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS native system sounds: "<<cases<<" cases, "<<guards<<" guards.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
