// Builds without the recompiler, Windows headers or a virtual CPU.
#include "resource_archive.hpp"
#include "file_io.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>
#include <cstring>
#include <map>
#include <random>
#include <winrecomp/hash.hpp>
#include "clock.hpp"
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
using namespace wa2::resource;
struct Bytes final : Source {
    std::vector<std::uint8_t> data;
    unsigned reads{};
    std::uint64_t size()const override{return data.size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> bytes)override {
        ++reads;
        if(at>data.size() || bytes.size()>data.size()-at)throw Error("test source range");
        std::copy_n(data.begin()+std::size_t(at),bytes.size(),bytes.begin());
    }
};
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const Error&){caught=true;}CHECK(caught);}
void put32(std::vector<std::uint8_t>& data,std::size_t at,std::uint32_t value){
    for(unsigned n=0;n<4;++n)data[at+n]=std::uint8_t(value>>(n*8));
}
std::shared_ptr<Bytes> synthetic(bool packed=false) {
    auto b=std::make_shared<Bytes>();b->data.resize(16+44+32);std::memcpy(b->data.data(),"KCAP",4);put32(b->data,12,1);
    put32(b->data,16,packed?1:0);std::memcpy(b->data.data()+20,"Test.Dat",8);put32(b->data,52,60);put32(b->data,56,packed?12:4);
    if(packed){put32(b->data,60,12);put32(b->data,64,3);b->data[68]=7;b->data[69]='a';b->data[70]='b';b->data[71]='c';}
    else {b->data[60]='a';b->data[61]='b';b->data[62]=0;b->data[63]='c';}return b;
}
std::uint32_t read32(std::istream& file){std::array<std::uint8_t,4>b{};file.read(reinterpret_cast<char*>(b.data()),4);CHECK(file);return b[0]|(std::uint32_t(b[1])<<8)|(std::uint32_t(b[2])<<16)|(std::uint32_t(b[3])<<24);}
// Independent scalar reference retains the original circular-dictionary
// algorithm, including all 4 KiB of dictionary state and terminating flags.
unsigned scalar_reference(std::span<const std::uint8_t> source,std::span<std::uint8_t> output,std::span<std::uint8_t,4096> ring) {
    std::size_t input=0,written=0;unsigned position=0xfee,flags=0;
    auto next=[&]{if(input==source.size())throw Error("truncated reference");return source[input++];};
    while(written<output.size()){
        flags>>=1;if(!(flags&0x100))flags=next()|0xff00;
        if(flags&1){auto value=next();output[written++]=value;ring[position]=value;position=(position+1)&4095;}
        else {unsigned low=next(),high=next(),offset=low|((high&0xf0)<<4),length=(high&15)+3;
            if(length>output.size()-written)throw Error("reference length");
            for(unsigned n=0;n<length;++n){auto value=ring[(offset+n)&4095];output[written++]=value;ring[position]=value;position=(position+1)&4095;}}
    }
    if(input!=source.size())throw Error("reference trailing input");return flags>>1;
}
unsigned dictionary_cases() {
    std::mt19937 random(0x437000);unsigned cases=0;
    for(unsigned n=0;n<2000;++n){
        std::vector<std::uint8_t> source;std::size_t size=0;
        const auto groups=1+random()%128;
        for(unsigned g=0;g<groups;++g){const auto flag=std::uint8_t(n%7==0?255:random());source.push_back(flag);
            const unsigned tokens=g+1==groups ? 1+random()%8 : 8;
            for(unsigned k=0;k<tokens;++k){
                if(flag&(1u<<k)){source.push_back(std::uint8_t(random()));++size;}
                else {
                    // Include distance 0 (4096), 1, overlapping repeats, both
                    // dictionary wraps, and references into the initial tail.
                    const auto position=unsigned((0xfee + size)&4095);
                    const unsigned distance=n%5==0 ? (g+k)%19 : random()%4096;
                    const auto offset=(position-distance)&4095,length=3+random()%16;
                    source.push_back(std::uint8_t(offset));source.push_back(std::uint8_t(((offset>>4)&0xf0)|(length-3)));size+=length;
                }
            }
        }
        std::array<std::uint8_t,4096> a{},b{};for(auto& value:a)value=std::uint8_t(random());b=a;
        std::vector<std::uint8_t> actual(size+32,0x5a),expected(size+32,0x5a);
        validate_lzss(source,size);
        const auto af=decode_lzss_dictionary(source,std::span(actual).first(size),a);
        const auto bf=scalar_reference(source,std::span(expected).first(size),b);
        CHECK(actual==expected);CHECK(a==b);CHECK(af==bf);++cases;
    }
    return cases;
}
unsigned fixed_copy_boundary_cases() {
    unsigned cases=0;
    for(unsigned prefix:{0,1,7,16,64})for(unsigned distance:{1,2,3,4,7,8,9,15,16,17,18,4095,4096})
    for(unsigned length=3;length<=18;++length)for(unsigned tail:{0,1,7,17,18}){
        std::vector<std::uint8_t> source;std::size_t written=0,flag_at=0;unsigned token=8;
        auto begin_token=[&]{if(token==8){flag_at=source.size();source.push_back(0);token=0;}};
        auto literal=[&](std::uint8_t byte){begin_token();source[flag_at]|=1u<<token++;source.push_back(byte);++written;};
        for(unsigned n=0;n<prefix;++n)literal(std::uint8_t(n*73+distance));
        begin_token();++token;const auto offset=unsigned((0xfee + written-distance)&4095);
        source.push_back(std::uint8_t(offset));source.push_back(std::uint8_t(((offset>>4)&0xf0)|(length-3)));written+=length;
        for(unsigned n=0;n<tail;++n)literal(std::uint8_t(n*31+length));
        std::array<std::uint8_t,4096> actual_ring{},expected_ring{};
        for(unsigned n=0;n<actual_ring.size();++n)actual_ring[n]=std::uint8_t(n*37+(n>>5));expected_ring=actual_ring;
        std::vector<std::uint8_t> actual(written+32,0x5a),expected(written+32,0x5a);
        const auto af=decode_lzss_dictionary(source,std::span(actual).subspan(16,written),actual_ring);
        const auto ef=scalar_reference(source,std::span(expected).subspan(16,written),expected_ring);
        CHECK(actual==expected);CHECK(actual_ring==expected_ring);CHECK(af==ef);++cases;
    }
    return cases;
}
struct DecodeSample {std::vector<std::uint8_t> source,output;};
std::int64_t median(std::vector<std::int64_t> samples){std::sort(samples.begin(),samples.end());return samples[samples.size()/2];}
struct DecodeBenchmark {std::int64_t native_ns{},reference_ns{},validation_ns{};std::uint64_t round_bytes{};};
DecodeBenchmark benchmark(std::vector<DecodeSample>& samples) {
    DecodeBenchmark result;for(auto& sample:samples)result.round_bytes+=sample.output.size()*8;
    std::vector<std::int64_t> native,reference,validation;
    for(unsigned round=0;round<5;++round){
        auto run=[&](auto function){const auto start=std::chrono::steady_clock::now();
            for(unsigned cycle=0;cycle<8;++cycle)for(auto& sample:samples){std::array<std::uint8_t,4096> ring{};std::fill_n(ring.begin(),0xfee,32);
                function(sample.source,sample.output,ring);std::atomic_signal_fence(std::memory_order_seq_cst);}
            return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();};
        if(round%2){native.push_back(run(decode_lzss_dictionary));reference.push_back(run(scalar_reference));}
        else {reference.push_back(run(scalar_reference));native.push_back(run(decode_lzss_dictionary));}
        const auto start=std::chrono::steady_clock::now();
        for(unsigned cycle=0;cycle<8;++cycle)for(auto& sample:samples){validate_lzss(sample.source,sample.output.size());std::atomic_signal_fence(std::memory_order_seq_cst);}
        validation.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
    }
    result.native_ns=median(native);result.reference_ns=median(reference);result.validation_ns=median(validation);return result;
}
int main(int argc,char** argv){try{
    CHECK(argc==4);unsigned checks=0;
    for(bool packed:{false,true}){
        const auto source=synthetic(packed);const auto archive=Archive::open(source);CHECK(source->reads==2);CHECK(archive.members().size()==1);CHECK(archive.find("tESt.dAT")==0);
        CHECK(archive.find("missing")==Archive::missing);auto bytes=archive.load(0);CHECK(bytes.size()==(packed?3:4));CHECK(bytes[0]=='a' && bytes[1]=='b');++checks;
    }
    CHECK(compare_names("A","a")==0);CHECK(compare_names("a","aa")<0);
    CHECK(compare_names(std::string("\x81\x40",2),std::string("\x81\x41",2))<0);
    std::array<std::uint8_t,9> overlap{};decode_lzss(std::array<std::uint8_t,6>{7,'a','b','c',0xee,0xf3},overlap);
    CHECK(std::string(overlap.begin(),overlap.end())=="abcabcabc");++checks;
    validate_lzss(std::array<std::uint8_t,6>{7,'a','b','c',0xee,0xf3},9);
    rejects([&]{validate_lzss(std::array<std::uint8_t,1>{1},9);});
    rejects([&]{validate_lzss(std::array<std::uint8_t,3>{0,0,15},9);});
    std::array<std::uint8_t,3> zeros{},spaces{};
    decode_lzss(std::array<std::uint8_t,3>{0,0,0},zeros,0);decode_lzss(std::array<std::uint8_t,3>{0,0,0},spaces,32);
    CHECK((zeros==std::array<std::uint8_t,3>{0,0,0}));CHECK((spaces==std::array<std::uint8_t,3>{32,32,32}));
    rejects([&]{decode_lzss(std::array<std::uint8_t,1>{1},overlap);});
    rejects([&]{decode_lzss(std::array<std::uint8_t,3>{0,0,15},overlap);});
    rejects([&]{std::array<std::uint8_t,1> out{};decode_lzss(std::array<std::uint8_t,3>{1,'a','b'},out);});
    for(auto kind:{0,1,2,3,4,5}){
        auto b=synthetic(true);
        if(kind==0)b->data.resize(7);if(kind==1)b->data[0]=0;
        if(kind==2)put32(b->data,12,0xffffffff);if(kind==3)put32(b->data,52,0xffffffff);
        if(kind==4)put32(b->data,60,0);if(kind==5)put32(b->data,56,7);
        rejects([&]{Archive::open(b).load(0);});++checks;
    }
    const std::filesystem::path root=argv[1];std::uint64_t entries=0,total=0,loads=0;
    std::map<std::string,Archive> archives;
    for(const char* name:{"bak.pak","char.pak","script.pak","grp.pak","fnt.pak","fon.pak","ck-gal.pak","BGM.PAK","SE.PAK","VOICE.PAK"}) {
        auto archive=Archive::open(wa2::platform::open_resource_file(root/name));
        for(std::size_t i=0;i<archive.members().size();++i){CHECK(archive.find(archive.members()[i].name)==i);++entries;}
        archives.emplace(name,std::move(archive));
    }
    const auto dictionary_count=dictionary_cases(),boundary_count=fixed_copy_boundary_cases();std::vector<DecodeSample> decode_samples;
    std::ifstream fixture(argv[2],std::ios::binary);std::array<char,8> magic{};fixture.read(magic.data(),8);
    CHECK((magic==std::array<char,8>{'W','2','R','S','v','1',0,0}));const auto samples=read32(fixture);
    for(unsigned n=0;n<samples;++n){
        const auto length=read32(fixture),stored=read32(fixture),expanded=read32(fixture);std::string label(length,'\0'),hash(64,'\0');
        fixture.read(label.data(),label.size());fixture.read(hash.data(),hash.size());std::vector<std::uint8_t> compressed(stored),expected(expanded);
        fixture.read(reinterpret_cast<char*>(compressed.data()),compressed.size());CHECK(fixture);decode_lzss(compressed,expected,32);CHECK(wr::sha256(expected)==hash);
        const auto slash=label.find('/');auto& archive=archives.at(label.substr(0,slash));const auto member=archive.find(label.substr(slash+1));CHECK(member!=Archive::missing);
        for(unsigned cycle=0;cycle<16;++cycle){auto output=archive.load(member);CHECK(output==expected);total+=output.size();++loads;}
        decode_samples.push_back({std::move(compressed),std::move(expected)});
    }
    const auto performance=benchmark(decode_samples);
    const auto& se=archives.at("SE.PAK");const auto long_sound=se.find("SE_9714.WAV");CHECK(long_sound!=Archive::missing);
    const auto sound=se.load(long_sound);CHECK(sound.size()==52993168);CHECK(std::memcmp(sound.data(),"RIFF",4)==0);
    std::array<std::uint8_t,16> tail{};auto raw=wa2::platform::open_resource_file(root/"SE.PAK");
    raw->read_at(se.members()[long_sound].offset+sound.size()-tail.size(),tail);
    CHECK(std::equal(tail.begin(),tail.end(),sound.end()-tail.size()));++loads;total+=sound.size();
    std::atomic<bool> okay{true};auto worker=[&]{try{for(unsigned i=0;i<32;++i){std::array<std::uint8_t,4>b{};raw->read_at(se.members()[long_sound].offset,b);if(std::memcmp(b.data(),"RIFF",4))okay=false;}}catch(...){okay=false;}};
    std::thread a(worker),b(worker);a.join();b.join();CHECK(okay);
    // Seeking alone does not extend the temporary file or allocate 4 GiB.
    auto* temporary=std::tmpfile();CHECK(temporary);CHECK(wa2::platform::seek_stream(temporary,0x100000011ull));
    CHECK(wa2::platform::stream_position(temporary)==0x100000011ull);CHECK(wa2::platform::seek_stream(temporary,0));
    CHECK(std::fclose(temporary)==0);
    CHECK(wa2::platform::Clock::file_time_from_unix_100ns(0)==116444736000000000ull);
    CHECK(wa2::platform::Clock::file_time_from_unix_100ns(-116444736000000000ll)==0);
    const auto started=std::chrono::steady_clock::now();const auto counter=wa2::platform::Clock::counter();
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    const auto duration=wa2::platform::Clock::counter()-counter;CHECK(duration>=15000000);
    CHECK(wa2::platform::Clock::milliseconds_since(started)>=15);
    wa2::platform::TimerResolution timers;CHECK(!timers.begin(0));CHECK(!timers.end(1));CHECK(timers.begin(1));CHECK(timers.end(1));
    std::ofstream report(argv[3]);report<<"{\"schema\":\"wa2.native-resource.v1\",\"archives\":"<<archives.size()<<",\"indexed_members\":"<<entries
        <<",\"loads\":"<<loads<<",\"decoded_bytes\":"<<total<<",\"edge_cases\":"<<checks<<",\"large_sound_bytes\":"<<sound.size()
        <<",\"dictionary_reference_cases\":"<<dictionary_count<<",\"fixed_copy_boundary_cases\":"<<boundary_count<<",\"archive_open_payload_reads\":0"
        <<",\"decode_comparison\":{\"rounds\":5,\"round_output_bytes\":"<<performance.round_bytes
        <<",\"native_median_ns\":"<<performance.native_ns<<",\"scalar_reference_median_ns\":"<<performance.reference_ns
        <<",\"validation_median_ns\":"<<performance.validation_ns<<",\"scope\":\"preloaded real resource samples; decoder only\"}"
        <<",\"stream_cursor_above_4gib\":true,\"concurrent_reads\":true,\"windows_headers\":false,\"virtual_cpu\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(report);
    std::cout<<"PASS native resources: "<<archives.size()<<" archives, "<<entries<<" names, "<<loads<<" loads; no CPU or Windows dependency\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
