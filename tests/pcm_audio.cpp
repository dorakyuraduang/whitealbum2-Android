// The native engine builds and tests without the original EXE, guest runtime,
// Windows headers, SDL, an audio device or any game window.
#include "pcm_audio.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>
using namespace wa2::audio;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
static void near(float got,float expected){CHECK(std::abs(got-expected)<0.00001f);}
static std::int16_t value(std::uint64_t frame){return std::int16_t((frame*17)%60000-30000);}
static void fill(Buffer& b,std::uint32_t at,std::uint32_t frames,std::uint64_t timeline){
    b.edit(at*4,frames*4,[&](auto bytes){for(std::uint32_t n=0;n<frames;++n){const auto v=std::bit_cast<std::uint16_t>(value(timeline+n));bytes[n*4]=bytes[n*4+2]=std::uint8_t(v);bytes[n*4+1]=bytes[n*4+3]=std::uint8_t(v>>8);}});
}
int main(int argc,char** argv){try{
    CHECK(argc<=2);unsigned cases=0;
    {
        Mixer mixer;auto b=mixer.create({48000,1,Encoding::signed16},8);
        const std::array<std::uint8_t,8> data{0,0,0,0x40,0,0x80,0xff,0x7f};
        b->edit(0,8,[&](auto out){std::copy(data.begin(),data.end(),out.begin());});
        unsigned half=0,ended=0;CHECK(b->notifications({{4,[&]{++half;}},{Notification::stopped,[&]{++ended;}}}));
        b->play(false);std::array<float,12> out{};mixer.render(out,48000);
        for(unsigned c=0;c<2;++c){near(out[c],0);near(out[2+c],.5);near(out[4+c],-1);near(out[6+c],32767/32768.f);near(out[8+c],0);near(out[10+c],0);}
        CHECK(!b->position().playing && half==1 && ended==1);b->stop();CHECK(ended==1);
        CHECK(b->seek(2) && !b->seek(8));b->play(true);CHECK(!b->notifications({}));
        mixer.render(out,48000);CHECK(b->position().play==6 && b->position().looping);b->stop();CHECK(ended==2);
        cases+=8;
    }
    {
        Mixer mixer;auto b=mixer.create({48000,2,Encoding::signed16},16);fill(*b,0,4,0);
        auto copy=mixer.duplicate(b);CHECK(mixer.live_buffers()==2);
        b->edit(0,4,[](auto out){out[0]=out[2]=0;out[1]=out[3]=0x40;});
        CHECK(copy->seek(0));copy->play(true);std::array<float,2> out{};mixer.render(out,48000);near(out[0],.5);near(out[1],.5);
        CHECK(b->position().play==0 && copy->position().play==4);
        CHECK(copy->pan(10000));CHECK(copy->seek(0));mixer.render(out,48000);near(out[0],0);near(out[1],.5);
        CHECK(copy->pan(-10000));CHECK(copy->seek(0));mixer.render(out,48000);near(out[0],.5);near(out[1],0);
        CHECK(copy->volume(-10000));mixer.render(out,48000);near(out[0],0);near(out[1],0);
        CHECK(!copy->volume(1) && !copy->volume(-10001) && !copy->pan(10001) && !copy->frequency(99) && !copy->frequency(200001));
        CHECK(copy->frequency(0) && copy->position().frequency==48000);
        b.reset();CHECK(mixer.live_buffers()==1);copy.reset();mixer.render(out,48000);CHECK(mixer.live_buffers()==0);cases+=10;
    }
    {
        Mixer mixer;auto b=mixer.create({24000,1,Encoding::signed16},8);
        b->edit(0,8,[](auto out){const std::array<std::uint8_t,8> data{0,0,0,0x40,0,0,0,0xc0};std::copy(data.begin(),data.end(),out.begin());});
        b->play(true);std::array<float,16> out{};mixer.render(out,48000);
        const std::array<float,8> expected{0,.25,.5,.25,0,-.25,-.5,-.25};for(unsigned n=0;n<8;++n){near(out[n*2],expected[n]);near(out[n*2+1],expected[n]);}
        CHECK(b->position().play==0);CHECK(b->frequency(48000));mixer.render(std::span(out).first(4),48000);CHECK(b->position().play==4);cases+=2;
    }
    for(const auto encoding:{Encoding::unsigned8,Encoding::signed16,Encoding::signed24,Encoding::signed32,Encoding::float32}){
        Mixer mixer;Format format{48000,1,encoding};auto b=mixer.create(format,format.frame_bytes()*4);
        b->edit(0,b->bytes(),[&](auto out){
            for(unsigned n=0;n<4;++n){auto* p=out.data()+n*format.sample_bytes();switch(encoding){
            case Encoding::unsigned8:p[0]=192;break;case Encoding::signed16:p[0]=0;p[1]=0x40;break;
            case Encoding::signed24:p[0]=p[1]=0;p[2]=0x40;break;case Encoding::signed32:p[0]=p[1]=p[2]=0;p[3]=0x40;break;
            case Encoding::float32:p[0]=p[1]=p[2]=0;p[3]=0x3f;break;}}
        });b->play(true);std::array<float,8> out{};mixer.render(out,48000);for(const auto sample:out)near(sample,.5);++cases;
    }
    {
        Mixer mixer;auto a=mixer.create({48000,1,Encoding::float32},4),b=mixer.create({48000,1,Encoding::float32},4);
        for(auto buffer:{a,b}){buffer->edit(0,4,[](auto out){out[0]=out[1]=0;out[2]=0x40;out[3]=0x3f;});buffer->play(true);}
        std::array<float,2> out{};mixer.render(out,48000);near(out[0],1);near(out[1],1);
        a->edit(0,4,[](auto out){out[0]=out[1]=0;out[2]=0xc0;out[3]=0x7f;});b->stop();mixer.render(out,48000);near(out[0],0);near(out[1],0);cases+=2;
    }
    // Three minutes of real PCM mixing with two one-second streaming halves.
    // Check every sample against its absolute timeline; a repeated half, missed
    // refill, seek or accumulated resampling error produces a mismatching value.
    std::uint64_t checked_frames=0;unsigned refills=0;
    {
        Mixer mixer;auto b=mixer.create({48000,2,Encoding::signed16},96000*4);fill(*b,0,96000,0);b->play(true);
        std::array<float,4800*2> out{};
        for(unsigned n=0;n<1800;++n){mixer.render(out,48000);for(unsigned k=0;k<4800;++k){const auto expected=value(checked_frames+k)/32768.f;near(out[k*2],expected);near(out[k*2+1],expected);}checked_frames+=4800;
            CHECK(b->position().play==(checked_frames%96000)*4);
            if(checked_frames%48000==0){const auto consumed=unsigned((checked_frames/48000-1)%2);fill(*b,consumed*48000,48000,checked_frames+48000);++refills;}
        }
        unsigned stopped=0;CHECK(!b->notifications({{Notification::stopped,[&]{++stopped;}}}));b->stop();CHECK(b->notifications({{Notification::stopped,[&]{++stopped;}}}));b->play(false);mixer.render(out,48000);b->stop();CHECK(stopped==1);++cases;
    }
    {
        Mixer mixer;auto b=mixer.create({48000,2,Encoding::signed16},4096);fill(*b,0,1024,0);b->play(true);
        std::thread output([&]{std::array<float,512> out{};for(unsigned n=0;n<2000;++n)mixer.render(out,48000);});
        for(unsigned n=0;n<2000;++n){auto duplicate=mixer.duplicate(b);duplicate->volume(-10000);duplicate->play(true);duplicate->edit(0,64,[](auto out){std::fill(out.begin(),out.end(),0);});duplicate->stop();}
        output.join();CHECK(mixer.live_buffers()==1);++cases;
    }
    if(argc==2){std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-pcm.v1\",\"cases\":"<<cases<<",\"checked_stream_frames\":"<<checked_frames<<",\"stream_seconds\":180,\"stream_refills\":"<<refills<<",\"concurrent_buffer_cycles\":2000,\"guest_runtime_dependency\":false,\"windows_dependency\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(report);}
    std::cout<<"PASS native PCM: "<<cases<<" cases, "<<checked_frames<<" exact streaming frames, "<<refills<<" half refills, 2000 concurrent buffer lifetimes\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
