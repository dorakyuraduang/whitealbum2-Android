#include "../platform/compat/acm.hpp"
#include "../port/guest_api.hpp"
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
using namespace wr;
Image fixture(){Image image;image.base=0x400000;image.size=65536;image.headers_size=4096;image.entry=0x401000;image.bytes.resize(8192);image.sections={{".text",4096,4096,4096,4096,0x60000020}};return image;}
struct Wave {std::vector<std::uint8_t> format,data;std::vector<std::int16_t> expected;};
Wave wave(unsigned tag){Wave w;w.format.resize(tag==2?50:tag==17?20:18);auto put=[&](unsigned at,U32 value,unsigned size=2){for(unsigned n=0;n<size;++n)w.format[at+n]=std::uint8_t(value>>(n*8));};
    put(0,tag);put(2,1);put(4,22050,4);put(12,tag==2 || tag==17?256:1);put(14,tag==2 || tag==17?4:8);
    if(tag==1){put(8,22050,4);w.data={0,128,255,64};w.expected={-32768,0,32512,-16384};}
    if(tag==6){put(8,22050,4);w.data={0xd5,0x55,0xaa,0x2a};w.expected={8,-8,32256,-32256};}
    if(tag==7){put(8,22050,4);w.data={0xff,0x7f,0x00,0x80};w.expected={0,0,-32124,32124};}
    if(tag==2){put(8,11290,4);put(16,32);put(18,500);put(20,7);const std::array<std::array<int,2>,7> coefficients{{{256,0},{512,-256},{0,0},{192,64},{240,0},{460,-208},{392,-232}}};for(unsigned n=0;n<7;++n){put(22+n*4,U32(coefficients[n][0]));put(24+n*4,U32(coefficients[n][1]));}
        w.data.resize(256);w.data[1]=16;w.data[3]=0xe8;w.data[4]=3;w.data[5]=0x84;w.data[6]=3;w.expected.assign(500,1000);w.expected[0]=900;}
    if(tag==17){put(8,11178,4);put(16,2);put(18,505);w.data.resize(256);w.data[0]=0xd2;w.data[1]=4;w.expected.assign(505,1234);}
    return w;
}
int main(int argc,char** argv){try{CHECK(argc==2);unsigned checks{};
    for(bool linear:{false,true})for(unsigned tag:{1u,2u,17u,6u,7u}){
        ProcessOptions options;options.prefer_linear_memory=linear;Process p(FunctionExecutor{[](Cpu&,Memory&){return false;}},options);wa2::platform::compat::install_acm(p);p.load(fixture());auto& m=p.memory;const auto work=p.allocate_bytes(65536),src=work,dst=work+128,header=work+256,input=work+1024,output=work+32768;
        const auto fixture=wave(tag);m.copy_in(src,fixture.format);m.copy_in(input,fixture.data);const auto call=[&](const char* name,std::initializer_list<U32> args){return p.callback(p.resolve("msacm32.dll",name),{args.begin(),args.size()});};
        CHECK(call("acmFormatSuggest",{0,src,dst,18,0})==0 && m.load(dst+14,16)==16 && m.load(dst+4,32)==22050);
        CHECK(call("acmStreamOpen",{work+512,0,src,dst,0,0,0,1})==0 && m.load(work+512,32)==0);
        CHECK(call("acmStreamOpen",{work+512,0,src,dst,0,0,0,0})==0);const auto stream=m.load(work+512,32);CHECK(stream);
        CHECK(call("acmStreamSize",{stream,U32(fixture.data.size()),work+516,0})==0 && m.load(work+516,32)==fixture.expected.size()*2);
        const auto first=U32(fixture.expected.size()/2*2);std::array<U32,21> fields{};fields[0]=84;fields[3]=input;fields[4]=U32(fixture.data.size());fields[7]=output;fields[8]=first;wr::port::write(p,header,fields);
        CHECK(call("acmStreamPrepareHeader",{stream,header,0})==0);CHECK(call("acmStreamClose",{stream,0})==513);
        CHECK(call("acmStreamConvert",{stream,header,0x10})==0 && m.load(header+20,32)==fixture.data.size() && m.load(header+36,32)==first);
        std::vector<std::int16_t> samples;for(unsigned n=0;n<first/2;++n)samples.push_back(std::bit_cast<std::int16_t>(std::uint16_t(m.load(output+n*2,16))));
        // Drain a decoded block through smaller destination windows. No input
        // is repeated, and an empty third call must not replay old PCM.
        m.store(header+16,0,32);const auto remaining=U32(fixture.expected.size()*2-first);m.store(header+32,first,32);
        while(samples.size()<fixture.expected.size()){CHECK(call("acmStreamConvert",{stream,header,0x20})==0 && m.load(header+20,32)==0);const auto used=m.load(header+36,32);CHECK(used>0 && used<=first);for(unsigned n=0;n<used/2;++n)samples.push_back(std::bit_cast<std::int16_t>(std::uint16_t(m.load(output+n*2,16))));}
        CHECK(samples==fixture.expected);CHECK(call("acmStreamConvert",{stream,header,0x20})==0 && m.load(header+20,32)==0 && m.load(header+36,32)==0);
        CHECK(call("acmStreamUnprepareHeader",{stream,header,0})==0 && call("acmStreamClose",{stream,0})==0);const auto report=p.state().reports.at("acm")();CHECK(report.find("\"live_streams\":0")!=report.npos && report.find("\"pending_pcm_bytes\":0")!=report.npos);CHECK(p.executed_instructions==0);++checks;
    }
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.portable-acm.v1\",\"codec_memory_cases\":"<<checks<<",\"pcm8_ms_adpcm_ima_alaw_mulaw\":true,\"split_output_and_no_old_pcm_replay\":true,\"prepared_header_lifetimes\":true,\"audio_hardware_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";std::cout<<"PASS native ACM bridge: five WAVE codecs, split output, empty tail and resource release in both memory backends.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
