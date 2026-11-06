#include "timeline.hpp"
#include "scene_controls.hpp"
#include "pcm_audio.hpp"
#include "pcm_stream.hpp"
#include <fstream>
#include <iostream>
#include <limits>

namespace g=wa2::game;namespace a=wa2::audio;namespace s=wa2::script;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};g::Timeline time;
    CHECK(time.sample(1000000)==1000000 && time.milliseconds()==1000 && !time.paused());++cases;
    CHECK(time.sample(1250000)==1250000 && time.pause(true,1500000) && time.microseconds()==1500000);++cases;
    CHECK(time.sample(5000000)==1500000 && !time.pause(true,6000000) && time.paused());++cases;
    CHECK(time.pause(false,10000000) && time.microseconds()==1500000 && time.sample(10250000)==1750000);++cases;
    CHECK(!time.pause(false,10500000) && time.microseconds()==2000000);++cases;
    rejects([&]{time.sample(-1);});rejects([&]{time.sample(10499999);});rejects([&]{time.pause(true,10499999);});guards+=3;CHECK(!time.paused() && time.microseconds()==2000000);++cases;
    {g::Timeline early;CHECK(early.pause(true,0));CHECK(early.sample(10000000)==0 && early.pause(false,11000000));CHECK(early.sample(11001000)==1000 && early.milliseconds()==1);++cases;}
    {g::Timeline wrap;const auto start=std::int64_t(std::numeric_limits<std::uint32_t>::max()-5)*1000;wrap.sample(start);wrap.sample(start+40000);CHECK(wrap.milliseconds()==34);++cases;}
    {g::Timeline end;end.sample(std::numeric_limits<std::int64_t>::max()-10);CHECK(end.sample(std::numeric_limits<std::int64_t>::max())==std::numeric_limits<std::int64_t>::max());++cases;}
    {g::Timeline loaded;loaded.rebase(5000000,9000000000);CHECK(loaded.sample(9000001000)==5001000 && loaded.milliseconds()==5001 && !loaded.paused());++cases;
        rejects([&]{loaded.rebase(-1,0);});rejects([&]{loaded.rebase(0,-1);});guards+=2;CHECK(loaded.microseconds()==5001000);}
    {g::Timeline loaded;loaded.rebase((std::int64_t(std::numeric_limits<std::uint32_t>::max())+4)*1000,0);CHECK(loaded.sample(1000)>0 && loaded.milliseconds()==4);++cases;}
    {g::Timeline loaded;loaded.rebase(std::numeric_limits<std::int64_t>::max()-1,0);rejects([&]{loaded.sample(2);});++guards;CHECK(loaded.microseconds()==std::numeric_limits<std::int64_t>::max()-1 && loaded.sample(1)==std::numeric_limits<std::int64_t>::max());++cases;}
    {g::Timeline clock;clock.sample(1000000);s::SceneControls controls([&]{return clock.milliseconds();});s::Command reset{195,{}};controls.dispatch(reset);
        std::array<s::Value,1> args{s::Value(1000)};s::Command wait{196,args};clock.sample(1250000);CHECK(!controls.dispatch(wait)->complete);
        clock.pause(true,1500000);clock.sample(100000000);CHECK(!controls.dispatch(wait)->complete);clock.pause(false,100000000);
        clock.sample(100499000);CHECK(!controls.dispatch(wait)->complete);clock.sample(100500000);CHECK(controls.dispatch(wait)->complete);++cases;}
    a::Mixer mixer;CHECK(!mixer.paused());const std::array<std::uint8_t,8> pcm{0,16,0,32,0,48,0,64};auto buffer=mixer.create({1000,1,a::Encoding::signed16},pcm.size());
    buffer->edit(0,buffer->bytes(),[&](auto out){std::copy(pcm.begin(),pcm.end(),out.begin());});unsigned ended{};
    CHECK(buffer->notifications({{a::Notification::stopped,[&]{++ended;}}}));buffer->play(false);
    auto stream=mixer.create_stream(1000,1,8);CHECK(stream->write(pcm)==pcm.size());stream->finish_input();stream->play();
    std::array<float,4> out{};mixer.render(out,1000);CHECK(out[0]==.25f && out[2]==.5f && buffer->position().play==4 && stream->status().consumed_frames==2);++cases;
    const auto status=stream->status();mixer.pause(true);mixer.pause(true);out.fill(1);mixer.render(out,1000);
    CHECK(mixer.paused() && out[0]==0 && out[3]==0 && buffer->position().play==4 && buffer->position().playing && !ended);++cases;
    CHECK(stream->status().consumed_frames==status.consumed_frames && stream->status().queued_frames==status.queued_frames && stream->status().underruns==status.underruns && !stream->status().finished);++cases;
    auto late=mixer.duplicate(buffer);late->seek(0);late->play(false);mixer.render(out,1000);CHECK(out[0]==0 && late->position().play==0);late->stop();++cases;
    mixer.pause(false);mixer.pause(false);mixer.render(out,1000);CHECK(out[0]==.75f && out[2]==1.f && ended==1 && stream->status().finished && stream->status().consumed_frames==4);++cases;
    mixer.render(out,1000);CHECK(out[0]==0 && out[2]==0 && ended==1);++cases;
    auto waiting=mixer.create_stream(1000,1,8);waiting->play();mixer.pause(true);mixer.render(out,1000);CHECK(waiting->status().underruns==0);waiting->stop();mixer.pause(false);mixer.render(out,1000);CHECK(waiting->status().finished && waiting->status().underruns==0);++cases;
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-lifecycle-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"suspended_pcm_consumption\":0,\"synthetic_pcm_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS shared native lifecycle clocks and PCM suspension: "<<cases<<" cases.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
