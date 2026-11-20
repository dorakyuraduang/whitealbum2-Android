// Finite codec/ABI checks using self-authored tones; no game startup or device.
#include "../recompiled/special/runtime.hpp"
#include "../engine/vorbis_stream.hpp"
#include "special_vorbis_fixture.hpp"
#include <winrecomp/win32_state.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>

#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed at ")+std::to_string(__LINE__)+": " #x);}while(false)
using wr::U32;
struct Result {std::vector<std::uint8_t> pcm;std::array<std::uint8_t,18> format{};unsigned calls{},eof{};};
Result decode(std::span<const std::uint8_t> ogg,bool native,bool linear,bool looping,unsigned chunk,unsigned capacity,unsigned repeats=1){
    wr::ProcessOptions options;options.data_root=std::filesystem::current_path()/"build";options.prefer_linear_memory=linear;
    wa2::special::Runtime runtime(options,{}, {},native);auto& p=runtime.process();p.load(wa2::special::initial_image());
    // Only the source CRT's allocation handle is initialized. No startup,
    // original executable, resource archive or OS audio service is invoked.
    const auto heap=p.callback(p.resolve("kernel32.dll","GetProcessHeap"),{});CHECK(heap);
    p.memory.store(0xbc3b0c,heap,32);p.cpu.fp.put16(0,0x7f);
    const auto object=p.allocate_bytes(0x22f8),input=p.allocate_bytes(std::max(chunk,4096u)),
        output=p.allocate_bytes(capacity+4),written=p.allocate_bytes(4),format=p.allocate_bytes(22);
    const auto call=[&](U32 entry,std::span<const U32> args){p.cpu.r[wr::ECX]=object;return p.callback(entry,args);};
    std::array<std::uint8_t,0x22f8> state;state.fill(0xa5);p.memory.copy_in(object,state);
    CHECK(call(0x458630,{})==0);p.memory.copy_out(object,state);
    CHECK(std::all_of(state.begin(),state.end(),[](auto v){return v==0;}));
    p.memory.store(object+0x22e2,looping,8);p.memory.store(format+18,0x55aa55aa,32);
    CHECK(call(0x458700,std::array<U32,2>{0,input})==0);
    CHECK(call(0x458700,std::array<U32,2>{format,0})==0);
    p.memory.copy_in(input,ogg.first(std::min<std::size_t>(ogg.size(),4096)));
    CHECK(call(0x458700,std::array<U32,2>{format,input})==1);
    Result result;p.memory.copy_out(format,result.format);const auto info=wa2::audio::query_vorbis_format(ogg);
    CHECK(p.memory.load(format,16)==1 && p.memory.load(format+2,16)==info.channels && p.memory.load(format+4,32)==info.rate);
    CHECK(p.memory.load(format+8,32)==info.rate*info.channels*2 && p.memory.load(format+12,16)==info.channels*2);
    CHECK(p.memory.load(format+14,16)==16 && p.memory.load(format+16,16)==0 && p.memory.load(format+18,32)==0x55aa55aa);
    p.memory.store(output+capacity,0xa55aa55a,32);std::size_t cursor{};bool cached{};
    for(unsigned n=0;n<1000;++n){
        const auto count=std::min<std::size_t>(chunk,ogg.size()-cursor);
        if(count)p.memory.copy_in(input,ogg.subspan(cursor,count));cursor+=count;
        const auto value=call(0x458890,std::array<U32,5>{input,U32(count),output,capacity,written});
        CHECK(value<=2);const auto size=p.memory.load(written,32);CHECK(size<=capacity);
        const auto begin=result.pcm.size();result.pcm.resize(begin+size);p.memory.copy_out(output,std::span(result.pcm).subspan(begin));++result.calls;
        CHECK(p.memory.load(output+capacity,32)==0xa55aa55a);
        if(size && !cached){
            CHECK(call(0x458700,std::array<U32,2>{format,0})==1);
            std::array<std::uint8_t,18> copy{};p.memory.copy_out(format,copy);CHECK(copy==result.format);cached=true;
        }
        if(value==(looping?2u:0u)){
            CHECK(cursor==ogg.size());if(++result.eof==repeats)break;cursor=0;
        }
    }
    CHECK(result.eof==repeats && !result.pcm.empty() && cached);
    if(!looping){CHECK(call(0x458890,std::array<U32,5>{input,0,output,capacity,written})==0 && p.memory.load(written,32)==0);}
    call(0x458660,{});call(0x458660,{});
    for(const auto offset:{0x22e2u,0x22e3u,0x22e4u})CHECK(p.memory.load(object+offset,8)==0);
    for(const auto offset:{0x2004u,0x22e8u,0x22ecu,0x22f0u})CHECK(p.memory.load(object+offset,32)==0);
    if(native){
        const auto report=p.state().reports.at("native_vorbis")();
        CHECK(report.find("\"errors\":0")!=report.npos && report.find("\"declined\":0")!=report.npos && report.find("\"live_streams\":0")!=report.npos);
    }
    CHECK(p.executed_instructions==0);return result;
}
unsigned guards(){
    wr::ProcessOptions options;options.data_root=std::filesystem::current_path()/"build";
    wa2::special::Runtime runtime(options);auto& p=runtime.process();p.load(wa2::special::initial_image());
    const auto object=p.allocate_bytes(0x22f8),data=p.allocate_bytes(4096),output=p.allocate_bytes(4096),written=p.allocate_bytes(4);
    p.cpu.r[wr::ECX]=object;CHECK(p.callback(0x458630,{})==0);
    unsigned checked{};
    for(unsigned reason=0;reason<6;++reason){
        std::array<std::uint8_t,0x22f8> before{},after{};before.fill(0xa5);p.memory.copy_in(object,before);
        const auto cpu=p.cpu;const auto identity=p.input_sha256;const auto byte=p.memory.load(0x458630,8);
        if(reason==0)p.input_sha256=std::string(64,'0');
        if(reason==1 || reason==2){
            p.memory.protect(0x458000,4096,wr::Memory::Read|wr::Memory::Write|wr::Memory::Execute);
            if(reason==2){p.memory.store(0x458630,byte^1u,8);p.memory.protect(0x458000,4096,wr::Memory::Read|wr::Memory::Execute);}
        }
        bool rejected{};
        try{
            p.cpu.r[wr::ECX]=object;
            if(reason<3)p.callback(0x458630,{});
            else p.callback(0x458890,std::array<U32,5>{data,64,reason==3?data:reason==4?0x458000:output,reason==5?4u*1024*1024+1:64u,written});
        }catch(const wr::GuestFault&){rejected=true;}
        CHECK(rejected);p.memory.copy_out(object,after);CHECK(before==after);
        p.cpu=cpu;p.input_sha256=identity;
        if(reason==1 || reason==2){
            p.memory.protect(0x458000,4096,wr::Memory::Read|wr::Memory::Write|wr::Memory::Execute);
            p.memory.store(0x458630,byte,8);p.memory.protect(0x458000,4096,wr::Memory::Read|wr::Memory::Execute);
        }
        ++checked;
    }
    return checked;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned native_cases{},reference_cases{};int maximum{};double maximum_rms{};std::uint64_t compared{};
    for(const auto ogg:{std::span<const std::uint8_t>(wa2::test::vorbis_1ch),std::span<const std::uint8_t>(wa2::test::vorbis_2ch)}){
        const auto reference=decode(ogg,false,false,true,4096,262144);++reference_cases;
        const auto native=decode(ogg,true,false,true,4096,262144);++native_cases;
        CHECK(reference.pcm.size()==native.pcm.size() && reference.format==native.format);
        const auto sample=[](auto& b,std::size_t at){const auto v=unsigned(b[at])|(unsigned(b[at+1])<<8);return v>=32768?int(v)-65536:int(v);};
        std::uint64_t squares{};
        for(std::size_t at=0;at<native.pcm.size();at+=2){const auto difference=std::abs(sample(native.pcm,at)-sample(reference.pcm,at));maximum=std::max(maximum,difference);squares+=std::uint64_t(difference)*difference;}
        const auto rms=std::sqrt(double(squares)/(native.pcm.size()/2));maximum_rms=std::max(maximum_rms,rms);compared+=native.pcm.size();
        CHECK(maximum<=16 && rms<=1.5);
        for(bool linear:{false,true})for(bool looping:{false,true})for(const auto [chunk,capacity]:{std::pair{257u,1000u},std::pair{4096u,262144u},std::pair{11337u,4093u}}){
            const auto fragmented=decode(ogg,true,linear,looping,chunk,capacity);
            CHECK(fragmented.pcm==native.pcm && fragmented.format==native.format);++native_cases;
        }
        const auto loop=decode(ogg,true,true,true,4096,262144,2);CHECK(loop.pcm.size()==native.pcm.size()*2);
        CHECK(std::equal(native.pcm.begin(),native.pcm.end(),loop.pcm.begin()) && std::equal(native.pcm.begin(),native.pcm.end(),loop.pcm.begin()+native.pcm.size()));++native_cases;
    }
    const auto admission=guards();
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.special-codec-check.v1\",\"source_reference_cases\":"<<reference_cases
        <<",\"shared_codec_cases\":"<<native_cases<<",\"guard_cases\":"<<admission<<",\"compared_pcm_bytes\":"<<compared
        <<",\"maximum_sample_error\":"<<maximum<<",\"maximum_rms_sample_error\":"<<maximum_rms
        <<",\"fragmentation_and_loop_restart\":true,\"non_looping_eof_drains_once\":true,\"metadata_and_cleanup_match\":true,\"paged_and_linear_memory\":true,"
          "\"original_audio_used\":false,\"audio_device_opened\":false,\"game_startup_called\":false,\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    CHECK(out);std::cout<<"PASS Special shared Vorbis: "<<native_cases<<" cases, "<<reference_cases<<" original static references, "<<admission<<" guards.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
