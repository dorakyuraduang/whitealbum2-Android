// Bounded PCM/Vorbis work in host RAM. No device, game or playback loop.
#include "pcm_audio.hpp"
#include "vorbis_stream.hpp"
#include "resource_archive.hpp"
#include "file_io.hpp"
#include <winrecomp/hash.hpp>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace au=wa2::audio;
int sample(unsigned frame,unsigned channel){return int((frame*3137+channel*12347+77)%60000)-30000;}
void fill(au::Buffer& buffer,unsigned channels){buffer.edit(0,buffer.bytes(),[&](auto bytes){
    for(unsigned n=0;n<bytes.size()/2;++n){const auto value=std::uint16_t(sample(n/channels,n%channels));bytes[n*2]=std::uint8_t(value);bytes[n*2+1]=std::uint8_t(value>>8);}});}
int main(int argc,char** argv){try{
    CHECK(argc==3);unsigned pcm_cases{};std::vector<std::uint8_t> pcm_bits;
    for(unsigned channels:{1,2})for(unsigned rate:{24000,48000,96000})for(bool loop:{false,true})for(bool faded:{false,true}){
        au::Mixer mixer;auto buffer=mixer.create({rate,std::uint16_t(channels),au::Encoding::signed16},65*channels*2);fill(*buffer,channels);
        CHECK(buffer->volume(faded?-1234:0) && buffer->pan(faded?-2345:0));buffer->play(loop);
        std::array<float,192*2> out{};mixer.render(out,48000);
        const float gain=float(std::pow(10.,(faded?-1234.:0.)/2000.)),right=gain*float(std::pow(10.,(faded?-2345.:0.)/2000.));
        for(unsigned n=0;n<192;++n){double at=double(n)*rate/48000.;const bool ended=!loop && at>=65;
            at=std::fmod(at,65.);const auto index=unsigned(at),next=index+1<65?index+1:loop?0:index;const float fraction=float(at-index);
            for(unsigned channel=0;channel<2;++channel){const auto part=std::min(channel,channels-1);
                const float a=sample(index,part)/32768.f,b=sample(next,part)/32768.f;
                const float expected=ended?0.f:std::clamp((a+(b-a)*fraction)*(channel?right:gain),-1.f,1.f);
                CHECK(std::bit_cast<std::uint32_t>(out[n*2+channel])==std::bit_cast<std::uint32_t>(expected));}}
        const auto bytes=std::as_bytes(std::span(out));pcm_bits.insert(pcm_bits.end(),reinterpret_cast<const std::uint8_t*>(bytes.data()),reinterpret_cast<const std::uint8_t*>(bytes.data()+bytes.size()));++pcm_cases;
    }
    std::array<std::uint64_t,8> mixing{};std::uint64_t checksum{};
    {au::Mixer mixer;auto a=mixer.create({48000,2,au::Encoding::signed16},4096*4),b=mixer.create({48000,1,au::Encoding::signed16},4096*2);
        fill(*a,2);fill(*b,1);a->volume(-1800);b->pan(2000);a->play(true);b->play(true);std::array<float,512*2> out;
        for(auto& ns:mixing){const auto begin=std::chrono::steady_clock::now();for(unsigned n=0;n<32;++n)mixer.render(out,48000);
            ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();checksum^=std::bit_cast<std::uint32_t>(out[0]);}}
    const auto archive=wa2::resource::Archive::open(wa2::platform::open_resource_file(argv[1]));CHECK(!archive.members().empty());
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.bounded-audio-latency.v1\",\"pcm_exact_cases\":"<<pcm_cases<<",\"pcm_sha256\":\""<<wr::sha256(pcm_bits)<<"\",\"mix_callbacks_per_sample\":32,\"mix_frames_per_callback\":512,\"mix_ns\":[";
    for(unsigned n=0;n<8;++n){if(n)report<<',';report<<mixing[n];}report<<"],\"tracks\":[";
    unsigned tracks{};for(auto index:{std::size_t(0),archive.members().size()/2,archive.members().size()-1}){
        const auto source=archive.open_member(index);const auto size=std::size_t(std::min<std::uint64_t>(65536,source->size()));std::vector<std::uint8_t> prefix(size);source->read_at(0,prefix);
        std::array<std::uint64_t,4> times{};std::string digest;std::size_t decoded_bytes{};
        for(auto& ns:times){std::vector<std::uint8_t> pcm;pcm.reserve(192000);std::array<std::uint8_t,8192> output;std::size_t at{};
            const auto begin=std::chrono::steady_clock::now();au::VorbisStream decoder;
            for(unsigned step=0;step<128 && pcm.size()<192000;++step){const auto count=std::min<std::size_t>(4096,prefix.size()-at);
                const auto result=decoder.push(std::span(prefix).subspan(at,count),std::span(output).first(std::min<std::size_t>(output.size(),192000-pcm.size())),prefix.size()==source->size() && at+count==prefix.size());
                at+=result.accepted;pcm.insert(pcm.end(),output.begin(),output.begin()+result.bytes);
                if(result.finished)break;
                CHECK(result.accepted || result.bytes);}
            ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();CHECK(!pcm.empty() && pcm.size()<=192000);
            const auto hash=wr::sha256(pcm);if(digest.empty()){digest=hash;decoded_bytes=pcm.size();}else CHECK(digest==hash && decoded_bytes==pcm.size());
        }
        if(tracks++)report<<',';report<<"{\"member\":"<<index<<",\"pcm_bytes\":"<<decoded_bytes<<",\"pcm_sha256\":\""<<digest<<"\",\"cold_decode_ns\":[";
        for(unsigned n=0;n<4;++n){if(n)report<<',';report<<times[n];}report<<"]}";
    }
    report<<"],\"audio_device_opened\":false,\"game_launched\":false,\"screen_access\":false,\"whole_game_fps_measured\":false,\"passed\":true}\n";
    std::cout<<"PASS 48 exact PCM cases and three bounded original-PAK Vorbis prefixes; no audio device.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
