// Generated streams and owned RAM only. No game assets, process or device.
#include "../engine/resource_archive.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);} while(false)
namespace r=wa2::resource;
using Bytes=std::vector<std::uint8_t>;
using Ring=std::array<std::uint8_t,4096>;
using Decoder=unsigned(*)(std::span<const std::uint8_t>,std::span<std::uint8_t>,std::span<std::uint8_t,4096>,unsigned*);
#ifdef WA2_LZSS_BASELINE
namespace wa2_lzss_before::resource {
unsigned decode_lzss_dictionary(std::span<const std::uint8_t>,std::span<std::uint8_t>,std::span<std::uint8_t,4096>,unsigned*);
void validate_lzss(std::span<const std::uint8_t>,std::size_t);
}
#endif
constexpr Decoder current=static_cast<Decoder>(r::decode_lzss_dictionary);
struct Stream {
    Bytes packed;std::size_t bytes{},tokens{},flag{};
    void literal(std::uint8_t value){begin();packed[flag]|=std::uint8_t(1u<<(tokens&7));packed.push_back(value);++bytes;++tokens;}
    void match(unsigned distance,unsigned length){CHECK(distance && distance<=4096 && length>=3 && length<=18);begin();
        const unsigned offset=unsigned((0xfee + bytes-distance)&4095);packed.push_back(std::uint8_t(offset));packed.push_back(std::uint8_t(((offset>>4)&0xf0)|(length-3)));bytes+=length;++tokens;}
    void begin(){if(!(tokens&7)){flag=packed.size();packed.push_back(0);}}
};
Ring initial(){Ring ring;for(unsigned n=0;n<ring.size();++n)ring[n]=std::uint8_t(n*71+(n>>5)*37);return ring;}
unsigned reference(const Stream& s,Bytes& out,Ring& ring,unsigned& tail){
    std::size_t input{},written{};unsigned flags{},position=0xfee;std::array<bool,4096> touched{};tail=0;
    while(written<out.size()){
        flags>>=1;if(!(flags&0x100)){CHECK(input<s.packed.size());flags=s.packed[input++]|0xff00;}
        if(flags&1){CHECK(input<s.packed.size());const auto value=s.packed[input++];out[written++]=value;ring[position]=value;touched[position]=true;position=(position+1)&4095;}
        else{CHECK(input+2<=s.packed.size());const unsigned low=s.packed[input++],high=s.packed[input++],offset=low|((high&0xf0)<<4),length=(high&15)+3;CHECK(length<=out.size()-written);
            for(unsigned n=0;n<length;++n){const auto at=(offset+n)&4095;if(at>=0xfee && !touched[at])tail|=1u<<(at-0xfee);
                const auto value=ring[at];out[written++]=value;ring[position]=value;touched[position]=true;position=(position+1)&4095;}}
    }
    CHECK(input==s.packed.size());return flags>>1;
}
struct Result {Bytes storage;Ring ring;unsigned flags{},tail{};std::string error;};
Result decode(Decoder function,std::span<const std::uint8_t> source,std::size_t size){
    Result result{Bytes(size+64,0xa5),initial()};
    try{result.flags=function(source,std::span(result.storage).subspan(32,size),result.ring,&result.tail);}
    catch(const std::exception& e){result.error=e.what();}
    CHECK(std::all_of(result.storage.begin(),result.storage.begin()+32,[](auto b){return b==0xa5;}));
    CHECK(std::all_of(result.storage.end()-32,result.storage.end(),[](auto b){return b==0xa5;}));return result;
}
void baseline_equal(const Result& actual,std::span<const std::uint8_t> source,std::size_t size){
#ifdef WA2_LZSS_BASELINE
    const auto before=decode(wa2_lzss_before::resource::decode_lzss_dictionary,source,size);
    CHECK(actual.storage==before.storage && actual.ring==before.ring && actual.flags==before.flags && actual.tail==before.tail && actual.error==before.error);
#endif
}
void valid(const Stream& stream){
    Bytes expected(stream.bytes);auto ring=initial();unsigned tail{};const auto flags=reference(stream,expected,ring,tail);
    const auto result=decode(current,stream.packed,stream.bytes);CHECK(result.error.empty());
    CHECK(std::equal(expected.begin(),expected.end(),result.storage.begin()+32));CHECK(result.ring==ring && result.flags==flags && result.tail==tail);
    baseline_equal(result,stream.packed,stream.bytes);r::validate_lzss(stream.packed,stream.bytes);
    auto no_tail=initial();Bytes output(stream.bytes);CHECK(r::decode_lzss_dictionary(stream.packed,output,no_tail)==flags && output==expected && no_tail==ring);
}
Stream pattern(unsigned mask,unsigned distance,unsigned length,unsigned prefix=4352,unsigned rounds=24){
    Stream stream;for(unsigned n=0;n<prefix;++n)stream.literal(std::uint8_t(n*37+n/11));
    for(unsigned round=0;round<rounds;++round)for(unsigned bit=0;bit<8;++bit){if(mask&(1u<<bit))stream.literal(std::uint8_t(round*17+bit*31));else stream.match(distance,length);}
    return stream;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned exact{},errors{},literal_transitions{},literal_failures{},literal_aliases{};
    constexpr std::array<unsigned,16> distances{1,2,3,4,7,8,9,15,16,17,18,31,127,255,4095,4096};
    for(unsigned mask=0;mask<256;++mask){valid(pattern(mask,distances[mask&15],3+(mask>>4)));++exact;}
    for(unsigned distance:distances)for(unsigned length=3;length<=18;++length){valid(pattern(0,distance,length,4096,16));++exact;}
    for(unsigned prefix:{0u,1u,7u,8u,17u,18u,4095u,4096u})for(unsigned mask:{0u,1u,0x55u,0xaau,0xfeu,0xffu}){
        valid(pattern(mask,distances[(prefix+mask)&15],3+(mask&15),prefix,4));++exact;}
    for(unsigned tail=0;tail<=160;++tail){auto stream=pattern(0,7,18);for(unsigned n=0;n<tail;++n)stream.literal(std::uint8_t(n));valid(stream);++exact;}
    // Near matches must keep the ordinary path: every candidate token is
    // checked, even when only the eighth offset or length differs.
    for(unsigned token=1;token<8;++token)for(unsigned variant=0;variant<32;++variant){
        auto candidate=pattern(255,1,18,4352,0);
        for(unsigned n=0;n<8;++n)candidate.match(n==token && variant<16?distances[variant]:1,n==token && variant>=16?3+(variant&15):18);
        for(unsigned n=0;n<160;++n)candidate.literal(std::uint8_t(n));valid(candidate);++exact;
    }
    // An apparent literal block may change at any one of its eight flag
    // positions. Exhaust all 256 flags there, including embedded 0xff data.
    for(unsigned group=0;group<8;++group)for(unsigned mask=0;mask<256;++mask){
        Stream candidate;for(unsigned n=0;n<8;++n)candidate.literal(255);
        for(unsigned part=0;part<8;++part)for(unsigned token=0;token<8;++token){
            if(part!=group || (mask&(1u<<token)))candidate.literal(255);
            else candidate.match(1+((part*8+token)%31),3+((part+token)%16));
        }
        valid(candidate);++exact;++literal_transitions;
    }
    // Error parity includes speculative bytes inside the declared destination;
    // a failed decode never publishes the private dictionary.
    auto stream=pattern(0x55,9,18);
    auto failed=[&](std::span<const std::uint8_t> source,std::size_t size){const auto result=decode(current,source,size);CHECK(!result.error.empty() && result.ring==initial());baseline_equal(result,source,size);
        auto validation_error=[&](auto function){try{function(source,size);}catch(const std::exception& e){return std::string(e.what());}return std::string{};};
        const auto error=validation_error(r::validate_lzss);CHECK(!error.empty());
#ifdef WA2_LZSS_BASELINE
        CHECK(error==validation_error(wa2_lzss_before::resource::validate_lzss));
#endif
        ++errors;};
    for(unsigned kind=0;kind<2;++kind){if(kind)stream=pattern(0,1,18);
        for(unsigned cut=1;cut<=64;++cut)failed(std::span(stream.packed).first(stream.packed.size()-cut),stream.bytes);
        for(unsigned cut=1;cut<=64;++cut)failed(stream.packed,stream.bytes-cut);}
    auto trailing=stream.packed;trailing.push_back(0);failed(trailing,stream.bytes);failed({},1);failed(std::array<std::uint8_t,1>{0},3);failed(std::array<std::uint8_t,2>{0,0},3);
    stream=pattern(255,1,18);
    for(unsigned cut=1;cut<=80;++cut){
        failed(std::span(stream.packed).first(stream.packed.size()-cut),stream.bytes);
        failed(stream.packed,stream.bytes-cut);literal_failures+=2;
    }
    // Output can overlap future packed flags even when each individual
    // eight-byte literal copy is nonoverlapping. Batch lookahead must not
    // change those earlier read/write dependencies.
    for(unsigned size:{72u,80u,128u,256u})for(unsigned offset:{size/2,size/2+9,size-8}){
        Stream aliased;for(unsigned n=0;n<size;++n)aliased.literal(std::uint8_t(n*37+3));
        const auto run=[&](Decoder decoder){
            Result result{Bytes(32+std::max(aliased.packed.size(),std::size_t(offset+size))+32,0xa5),initial()};
            std::copy(aliased.packed.begin(),aliased.packed.end(),result.storage.begin()+32);
            try{result.flags=decoder(std::span<const std::uint8_t>(result.storage).subspan(32,aliased.packed.size()),
                std::span(result.storage).subspan(32+offset,size),result.ring,&result.tail);}
            catch(const std::exception& e){result.error=e.what();}
            CHECK(std::all_of(result.storage.begin(),result.storage.begin()+32,[](auto b){return b==0xa5;}));
            CHECK(std::all_of(result.storage.end()-32,result.storage.end(),[](auto b){return b==0xa5;}));return result;
        };
        const auto actual=run(current);
#ifdef WA2_LZSS_BASELINE
        const auto before=run(wa2_lzss_before::resource::decode_lzss_dictionary);
        CHECK(actual.storage==before.storage && actual.ring==before.ring && actual.flags==before.flags && actual.tail==before.tail && actual.error==before.error);
#endif
        ++literal_aliases;
    }
    struct Timing {unsigned mask,distance;std::array<std::uint64_t,5> ns;};std::vector<Timing> timings;
#ifdef WA2_LZSS_BASELINE
    std::vector<Timing> before_timings;
#endif
    std::uint64_t fingerprint{};
    constexpr unsigned Repetitions=16;
    for(auto [mask,distance]:std::array<std::pair<unsigned,unsigned>,6>{{{0,1},{0,4},{0,16},{0,32},{0x55,9},{0xff,1}}}){
        auto sample=pattern(mask,distance,18,4352,8192);Bytes output(sample.bytes);auto ring=initial();Timing item{mask,distance};
#ifdef WA2_LZSS_BASELINE
        Timing before{mask,distance};
#endif
        for(unsigned round=0;round<5;++round){
            auto measure=[&](Decoder decoder){const auto start=std::chrono::steady_clock::now();
                for(unsigned repeat=0;repeat<Repetitions;++repeat)fingerprint+=decoder(sample.packed,output,ring,nullptr)+output[sample.bytes/2];
                return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();};
#ifdef WA2_LZSS_BASELINE
            if(round&1){item.ns[round]=measure(current);before.ns[round]=measure(wa2_lzss_before::resource::decode_lzss_dictionary);}
            else {before.ns[round]=measure(wa2_lzss_before::resource::decode_lzss_dictionary);item.ns[round]=measure(current);}
#else
            item.ns[round]=measure(current);
#endif
        }
        timings.push_back(item);
#ifdef WA2_LZSS_BASELINE
        before_timings.push_back(before);
#endif
    }
    std::ofstream file(argv[1]);file<<"{\"schema\":\"wa2.lzss-groups.v1\",\"exact_output_dictionary_flags_and_tail_cases\":"<<exact<<",\"unchanged_failure_cases\":"<<errors
        <<",\"literal_block_transition_cases\":"<<literal_transitions<<",\"literal_block_failure_cases\":"<<literal_failures
        <<",\"literal_alias_cases\":"<<literal_aliases<<",\"timings\":[";
    for(unsigned n=0;n<timings.size();++n){if(n)file<<',';const auto& item=timings[n];file<<"{\"literal_mask\":"<<item.mask<<",\"distance\":"<<item.distance<<",\"repetitions\":"<<Repetitions<<",\"ns\":[";
        for(unsigned k=0;k<5;++k){if(k)file<<',';file<<item.ns[k];}file<<']';
#ifdef WA2_LZSS_BASELINE
        file<<",\"before_ns\":[";for(unsigned k=0;k<5;++k){if(k)file<<',';file<<before_timings[n].ns[k];}file<<']';
#endif
        file<<'}';}
    file<<"],\"fingerprint\":"<<fingerprint<<",\"guard_bytes_checked\":true,\"original_resources_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(file.good());
    std::cout<<"PASS LZSS groups: "<<exact<<" exact streams, "<<errors<<" preserved failures.\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
