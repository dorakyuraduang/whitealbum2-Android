// Native streaming and the original complete decoder process the same real Ogg.
#include "native_vorbis.hpp"
#include "wa2_win32.hpp"
#include "support.hpp"
#include "resource_archive.hpp"
#include "file_io.hpp"
#include "vorbis_stream.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>
bool compiled_step(wr::Cpu&,wr::Memory&,std::uint64_t&);
using wr::U32;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
struct Result {std::vector<std::uint8_t> pcm;std::array<std::uint8_t,18> format{};double ms{},lifecycle_ms{};unsigned eof{},calls{};};
Result original(const std::filesystem::path& image,std::span<const std::uint8_t> ogg,bool native,unsigned output_size,unsigned repeats=1,bool looping=true,bool paged=false){
    wr::ProcessOptions options;options.instruction_budget=std::numeric_limits<std::uint64_t>::max();
    wr::Process p(compiled_step,options);wr::install_wa2_win32(p);wr::install_wa2_native_vorbis(p,native);p.load(wr::Image::load(image));
    p.memory.store(0xbc6bfc,p.callback(p.resolve("kernel32.dll","GetProcessHeap"),{}),32);
    p.memory.store(0xbc830c,1,32);p.memory.store(0xbc8310,1,32);
    if(paged){p.memory.map(0x06000000,1,wr::Memory::Read|wr::Memory::Write);CHECK(!p.memory.execution_scope().linear_base());}
    const auto object=p.allocate_bytes(0x22f8),input=p.allocate_bytes(4096),output=p.allocate_bytes(output_size+4),written=p.allocate_bytes(4),format=p.allocate_bytes(22);
    Result result;const auto lifecycle_started=std::chrono::steady_clock::now();
    std::array<std::uint8_t,0x22f8> initial{};initial.fill(0xa5);p.memory.copy_in(object,initial);
    p.cpu.r[wr::ECX]=object;CHECK(p.callback(0x4566d0,{})==0);p.memory.copy_out(object,initial);
    CHECK(std::all_of(initial.begin(),initial.end(),[](auto value){return value==0;}));
    p.memory.store(object+0x22e2,looping?1:0,8);p.memory.store(format+18,0x55aa55aa,32);
    p.cpu.r[wr::ECX]=object;CHECK(p.callback(0x4567b0,std::array<U32,2>{0,input})==0);
    p.cpu.r[wr::ECX]=object;CHECK(p.callback(0x4567b0,std::array<U32,2>{format,0})==0);
    p.memory.copy_in(input,ogg.first(4096));p.cpu.r[wr::ECX]=object;CHECK(p.callback(0x4567b0,std::array<U32,2>{format,input})==1);
    p.memory.copy_out(format,result.format);CHECK(p.memory.load(format+18,32)==0x55aa55aa);
    const auto info=wa2::audio::query_vorbis_format(ogg.first(58));
    CHECK(p.memory.load(format,16)==1 && p.memory.load(format+2,16)==info.channels && p.memory.load(format+4,32)==info.rate);
    CHECK(p.memory.load(format+8,32)==info.rate*info.channels*2 && p.memory.load(format+12,16)==info.channels*2);
    CHECK(p.memory.load(format+14,16)==16 && p.memory.load(format+16,16)==0);
    result.lifecycle_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-lifecycle_started).count();
    p.memory.store(output+output_size,0xa55aa55a,32);
    std::size_t cursor=0;bool cached_format=false;const auto started=std::chrono::steady_clock::now();
    for(unsigned call=0;call<100000;++call){
        const auto count=std::min<std::size_t>(4096,ogg.size()-cursor);if(count)p.memory.copy_in(input,ogg.subspan(cursor,count));cursor+=count;
        p.cpu.r[wr::ECX]=object;const auto value=p.callback(0x456930,std::array<U32,5>{input,U32(count),output,output_size,written});
        CHECK(value<=2 && (!looping || value));const auto size=p.memory.load(written,32);CHECK(size<=output_size);
        const auto begin=result.pcm.size();result.pcm.resize(begin+size);p.memory.copy_out(output,std::span(result.pcm).subspan(begin,size));++result.calls;
        CHECK(p.memory.load(output+output_size,32)==0xa55aa55a);
        if(!cached_format && size){
            CHECK(p.memory.load(object+0x22e3,8));p.cpu.r[wr::ECX]=object;
            CHECK(p.callback(0x4567b0,std::array<U32,2>{format,0})==1);
            std::array<std::uint8_t,18> cached{};p.memory.copy_out(format,cached);CHECK(cached==result.format);
            CHECK(p.memory.load(format+18,32)==0x55aa55aa);cached_format=true;
        }
        if(value==(looping?2u:0u)){CHECK(cursor==ogg.size());++result.eof;if(result.eof==repeats)break;cursor=0;}
    }
    result.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();CHECK(result.eof==repeats);
    CHECK(cached_format);p.cpu.r[wr::ECX]=object;p.callback(0x456700,{});
    for(const auto offset:{0x22e2u,0x22e3u,0x22e4u})CHECK(p.memory.load(object+offset,8)==0);
    for(const auto offset:{0x2004u,0x22e8u,0x22ecu,0x22f0u})CHECK(p.memory.load(object+offset,32)==0);
    p.cpu.r[wr::ECX]=object;p.callback(0x456700,{}); // Cleanup before first decode and repeated cleanup are both valid.
    if(native){const auto report=p.state().reports.at("native_vorbis")();CHECK(report.find("\"errors\":0")!=std::string::npos);CHECK(report.find("\"declined\":0")!=std::string::npos);CHECK(report.find("\"live_streams\":0")!=std::string::npos);CHECK(report.find("\"constructors\":1")!=std::string::npos);CHECK(report.find("\"resets\":2")!=std::string::npos);CHECK(report.find("\"cached_format_queries\":1")!=std::string::npos);}
    return result;
}
Result portable(std::span<const std::uint8_t> ogg,unsigned chunk,unsigned capacity){
    wa2::audio::VorbisStream decoder;Result result;std::vector<std::uint8_t> output(capacity);std::size_t cursor=0;
    for(unsigned n=0;n<100000;++n){const auto count=std::min<std::size_t>(chunk,ogg.size()-cursor);
        const auto r=decoder.push(ogg.subspan(cursor,count),output,cursor+count==ogg.size());cursor+=r.accepted;
        CHECK(r.bytes<=capacity && decoder.buffered_bytes()<=4*1024*1024+131072);
        result.pcm.insert(result.pcm.end(),output.begin(),output.begin()+r.bytes);++result.calls;if(r.finished){result.eof=1;break;}}
    CHECK(result.eof);return result;
}
int main(int argc,char** argv){try{
    CHECK(argc==4);const auto root=std::filesystem::path(argv[2]);
    const auto archive=wa2::resource::Archive::open(wa2::platform::open_resource_file(root/"BGM.PAK"));
    std::vector<std::size_t> selected{0,archive.members().size()/2,archive.members().size()-1};
    std::ofstream proof(argv[3]);proof<<"{\"schema\":\"wa2.native-vorbis.v1\",\"tracks\":[";bool first=true;
    for(const auto index:selected){const auto ogg=archive.load(index);CHECK(ogg.size()>=4096 && std::equal(ogg.begin(),ogg.begin()+4,"OggS"));
        const auto reference=original(argv[1],ogg,false,262144),native=original(argv[1],ogg,true,262144);
        std::cerr<<archive.members()[index].name<<" bytes "<<reference.pcm.size()<<'/'<<native.pcm.size()<<" ms "<<reference.ms<<'/'<<native.ms<<'\n';
        CHECK(reference.pcm.size()==native.pcm.size() && !native.pcm.empty() && reference.format==native.format);
        std::uint64_t square=0;int maximum=0;
        auto sample=[](const auto& bytes,std::size_t at){const auto v=unsigned(bytes[at])|(unsigned(bytes[at+1])<<8);return v>=32768?int(v)-65536:int(v);};
        for(std::size_t at=0;at<native.pcm.size();at+=2){const auto difference=std::abs(sample(native.pcm,at)-sample(reference.pcm,at));maximum=std::max(maximum,difference);square+=std::uint64_t(difference)*difference;}
        const auto rms=std::sqrt(double(square)/(native.pcm.size()/2));std::cerr<<"PCM error max/rms "<<maximum<<'/'<<rms<<'\n';CHECK(maximum<=16 && rms<=1.5);
        const auto tiny=portable(ogg,257,1000);CHECK(tiny.pcm==native.pcm);
        const auto uneven=portable(ogg,11337,4093);CHECK(uneven.pcm==native.pcm);
        if(index==selected.front()){const auto loop=original(argv[1],ogg,true,262144,2);CHECK(loop.pcm.size()==native.pcm.size()*2);CHECK(std::equal(native.pcm.begin(),native.pcm.end(),loop.pcm.begin()));CHECK(std::equal(native.pcm.begin(),native.pcm.end(),loop.pcm.begin()+native.pcm.size()));const auto paged=original(argv[1],ogg,true,262144,1,true,true);CHECK(paged.pcm==native.pcm && paged.format==native.format);}
        if(index==selected.front()){
            const auto stopped=original(argv[1],ogg,true,262144,1,false),old_stopped=original(argv[1],ogg,false,262144,1,false);
            CHECK(stopped.pcm==native.pcm && old_stopped.pcm==reference.pcm);
            CHECK(stopped.calls<ogg.size()/4096+50 && old_stopped.calls<ogg.size()/4096+50);
        }
        if(!first)proof<<',';first=false;proof<<"{\"member\":\""<<archive.members()[index].name<<"\",\"pcm_bytes\":"<<native.pcm.size()<<",\"original_ms\":"<<reference.ms<<",\"native_ms\":"<<native.ms<<",\"original_lifecycle_ms\":"<<reference.lifecycle_ms<<",\"native_lifecycle_ms\":"<<native.lifecycle_ms<<",\"maximum_sample_error\":"<<maximum<<",\"rms_sample_error\":"<<rms<<'}';
    }
    bool rejected=false;try{wa2::audio::VorbisStream bad;std::array<std::uint8_t,64> in{},out{};bad.push(in,out);}catch(const wa2::audio::VorbisError&){rejected=true;}CHECK(rejected);
    proof<<"],\"fragmentation_matches\":true,\"loop_restart_matches\":true,\"non_looping_eof_matches\":true,\"original_format_query_preserved\":true,\"native_lifecycle\":true,\"metadata_matches_original\":true,\"constructor_zeroes_object\":true,\"repeated_cleanup\":true,\"cached_format_matches\":true,\"paged_memory_matches\":true,\"output_canary_unchanged\":true,\"screen_access\":false,\"passed\":true}\n";CHECK(proof);
    std::cout<<"PASS native Vorbis tracks, legacy PCM comparison, fragmented input/output, bounded staging and malformed input\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
