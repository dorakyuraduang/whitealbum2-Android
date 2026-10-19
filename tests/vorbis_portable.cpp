// This executable links ordinary engine/platform modules, with no legacy CPU.
#include "vorbis_stream.hpp"
#include "resource_archive.hpp"
#include "file_io.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <vector>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
std::vector<std::uint8_t> decode(std::span<const std::uint8_t> data,unsigned chunk,unsigned capacity){
    wa2::audio::VorbisStream decoder;std::vector<std::uint8_t> result,output(capacity);std::size_t cursor{};bool finished{};
    for(unsigned n=0;n<200000;++n){const auto size=std::min<std::size_t>(chunk,data.size()-cursor);const auto r=decoder.push(data.subspan(cursor,size),output,cursor+size==data.size());cursor+=r.accepted;
        result.insert(result.end(),output.begin(),output.begin()+r.bytes);CHECK(decoder.buffered_bytes()<5*1024*1024);
        if(r.finished){CHECK(cursor==data.size() && r.channels && r.rate);finished=true;break;}}
    CHECK(finished && !result.empty());return result;
}
int main(int argc,char** argv){try{
    CHECK(argc==3);const auto archive=wa2::resource::Archive::open(wa2::platform::open_resource_file(std::filesystem::path(argv[1])/"BGM.PAK"));
    std::uint64_t total{},identification_headers{};unsigned cases{},metadata_cases{},audio_archives{};
    for(const auto member:{std::size_t(0),archive.members().size()/2,archive.members().size()-1}){
        const auto source=archive.load(member);const auto reference=decode(source,4096,262144);
        const auto info=wa2::audio::query_vorbis_format(source);CHECK(info==wa2::audio::query_vorbis_format(std::span(source).first(58)));
        CHECK(info.channels && info.rate);++metadata_cases;
        for(std::size_t n=0;n<58;++n){bool rejected=false;try{wa2::audio::query_vorbis_format(std::span(source).first(n));}catch(const wa2::audio::VorbisError&){rejected=true;}CHECK(rejected);++metadata_cases;}
        for(std::size_t n=0;n<58;++n){std::vector<std::uint8_t> bad(source.begin(),source.begin()+58);bad[n]^=0x80;bool rejected=false;try{wa2::audio::query_vorbis_format(bad);}catch(const wa2::audio::VorbisError&){rejected=true;}CHECK(rejected);++metadata_cases;}
        CHECK(decode(source,257,1000)==reference);CHECK(decode(source,11337,4093)==reference);total+=reference.size();cases+=3;
    }
    bool rejected=false;try{wa2::audio::VorbisStream bad;std::array<std::uint8_t,64> bytes{};bad.push(bytes,bytes);}catch(const wa2::audio::VorbisError&){rejected=true;}CHECK(rejected);
    // The new format-query path replaces the old library for every stream.
    // Check all uncompressed Ogg identification pages in the supplied audio
    // archives, including voices not selected by the full PCM comparison.
    for(const auto* name:{"BGM.PAK","SE.PAK","SOUND.PAK","VOICE.PAK"}){
        const auto path=std::filesystem::path(argv[1])/name;if(!std::filesystem::is_regular_file(path))continue;
        const auto file=wa2::platform::open_resource_file(path);const auto audio=wa2::resource::Archive::open(file);++audio_archives;
        std::uint64_t headers{};
        for(const auto& member:audio.members()){
            if(member.compressed || member.stored_size<4)continue;
            std::array<std::uint8_t,58> page{};const auto bytes=std::min<std::size_t>(member.stored_size,page.size());
            file->read_at(member.offset,std::span(page).first(bytes));
            if(!std::equal(page.begin(),page.begin()+4,"OggS"))continue;
            try{const auto info=wa2::audio::query_vorbis_format(std::span(page).first(bytes));CHECK(info.channels && info.rate);}
            catch(const std::exception& e){throw std::runtime_error(std::string(name)+"/"+member.name+": "+e.what());}
            ++headers;
        }
        identification_headers+=headers;std::cerr<<name<<": "<<headers<<" Ogg identification pages accepted\n";
    }
    CHECK(identification_headers>=3);
    std::ofstream proof(argv[2]);proof<<"{\"schema\":\"wa2.portable-vorbis.v1\",\"tracks\":3,\"fragmentation_cases\":"<<cases<<",\"metadata_cases\":"<<metadata_cases<<",\"audio_archives\":"<<audio_archives<<",\"identification_headers\":"<<identification_headers<<",\"pcm_bytes\":"<<total<<",\"bounded_staging\":true,\"malformed_input_rejected\":true,\"legacy_runtime_linked\":false,\"passed\":true}\n";CHECK(proof);
    std::cout<<"PASS standalone Vorbis: three real tracks, nine input/output fragmentations, EOF, malformed data, bounded staging\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
