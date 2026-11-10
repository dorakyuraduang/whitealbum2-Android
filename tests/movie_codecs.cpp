// Decode into owned memory only: no window, renderer, device or game process.
#include "../engine/movie_reader.hpp"
#include "../platform/file_io.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>

namespace m=wa2::movie;namespace r=wa2::resource;
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": "+#x);}while(false)
unsigned checks{},guards{};
constexpr unsigned prefix=8;
struct Stamp {std::int64_t time{};std::size_t bytes{};std::uint64_t hash{14695981039346656037ull};bool operator==(const Stamp&)const=default;};
Stamp stamp(const m::Frame& frame){
    Stamp result;
    const auto add=[&](const std::uint8_t* bytes,std::size_t size){result.bytes+=size;for(std::size_t n=0;n<size;++n){result.hash^=bytes[n];result.hash*=1099511628211ull;}};
    if(const auto* video=std::get_if<m::VideoFrame>(&frame)){
        CHECK(video->owner && video->width>0 && video->height>0 && video->time_us>=0 && video->duration_us>0);
        result.time=video->time_us;
        for(const auto& plane:video->planes){CHECK(plane.pixels && plane.pitch>=plane.width && plane.width>0 && plane.height>0);for(int y=0;y<plane.height;++y)add(plane.pixels+y*plane.pitch,plane.width);}
    }else{
        const auto& audio=std::get<m::AudioFrame>(frame);CHECK(audio.rate>=1000 && audio.channels>=1 && audio.channels<=2 && !audio.pcm16.empty() && audio.pcm16.size()%(2*audio.channels)==0 && audio.time_us>=0);
        result.time=audio.time_us;add(audio.pcm16.data(),audio.pcm16.size());
    }return result;
}
void bounded(const m::ReaderStatistics& stats){CHECK(stats.largest_read<=32768 && stats.queued_packet_bytes==0 && stats.peak_packet_count==0 && stats.peak_packet_bytes==0);}
template<class F>void rejects(F action,std::string_view message={}){
    bool caught{};try{action();}catch(const std::exception& error){caught=message.empty() || std::string_view(error.what()).find(message)!=std::string_view::npos;}
    CHECK(caught);++guards;
}
struct HeaderOverride final:r::Source {
    std::shared_ptr<r::Source> source;std::vector<std::uint8_t> header;
    std::uint64_t size()const override{return source->size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{
        source->read_at(at,out);if(at<header.size()){const auto count=std::min(out.size(),header.size()-std::size_t(at));std::copy_n(header.begin()+std::size_t(at),count,out.begin());}
    }
};
std::uint32_t u32(const std::vector<std::uint8_t>& b,std::size_t at){CHECK(at+4<=b.size());return b[at]|std::uint32_t(b[at+1])<<8|std::uint32_t(b[at+2])<<16|std::uint32_t(b[at+3])<<24;}
std::uint64_t u64(const std::vector<std::uint8_t>& b,std::size_t at){return u32(b,at)|std::uint64_t(u32(b,at+4))<<32;}
void unsupported(std::shared_ptr<r::Source> source,bool video){
    auto altered=std::make_shared<HeaderOverride>();altered->source=std::move(source);altered->header.resize(30);altered->source->read_at(0,altered->header);
    const auto length=u64(altered->header,16);CHECK(length>=30 && length<=1024*1024 && length<=altered->size());altered->header.resize(length);altered->source->read_at(0,altered->header);
    auto& bytes=altered->header;std::size_t at=30;bool changed{};
    const std::array<std::uint8_t,16> stream_guid{0x91,0x07,0xdc,0xb7,0xb7,0xa9,0xcf,0x11,0x8e,0xe6,0x00,0xc0,0x0c,0x20,0x53,0x65};
    for(unsigned n=0;n<u32(bytes,24);++n){
        CHECK(at+24<=bytes.size());const auto size=u64(bytes,at+16);CHECK(size>=24 && size<=bytes.size()-at);
        if(std::equal(stream_guid.begin(),stream_guid.end(),bytes.begin()+at)){
            CHECK(size>=80);const bool match=video?u32(bytes,at+24)==0xbc19efc0:u32(bytes,at+24)==0xf8699e40;
            if(match){if(video){CHECK(size>=109);std::memcpy(bytes.data()+at+105,"AV01",4);}else{bytes[at+78]=0x0a;bytes[at+79]=0x00;}changed=true;break;}
        }at+=size;
    }
    CHECK(changed);rejects([&]{m::Reader reader(altered);},video?"unsupported movie video codec: av1":"unsupported movie audio codec: wmavoice");
}
int main(int argc,char** argv){try{
    CHECK(argc==4);const bool full=std::string_view(argv[3])=="full";CHECK(full || std::string_view(argv[3])=="prefix");
    auto source=wa2::platform::open_resource_file(std::filesystem::u8path(argv[1]));
    const auto until=std::chrono::steady_clock::now()+std::chrono::minutes(5);
    std::array<Stamp,prefix> video_reference{},audio_reference{};m::Information info;
    {
        m::Reader reader(source);info=reader.information();CHECK(info.width>0 && info.height>0 && info.audio_channels==2 && info.audio_rate>=44100);
        unsigned video{},audio{},frames{};
        while(video<prefix || audio<prefix){auto frame=reader.next();CHECK(frame && ++frames<10000 && std::chrono::steady_clock::now()<until);
            if(std::holds_alternative<m::VideoFrame>(*frame)){if(video<prefix)video_reference[video++]=stamp(*frame);}
            else if(audio<prefix)audio_reference[audio++]=stamp(*frame);
        }bounded(reader.statistics());
    }
    std::uint64_t audio_count{},video_count{},samples{},video_bytes{},audio_bytes{};unsigned comparisons{};
    std::optional<m::VideoFrame> held;std::uint8_t held_pixel{};
    {
        m::Reader reader(source,{},m::Track::video);std::int64_t previous=-1;
        while(auto frame=reader.next()){
            CHECK(std::holds_alternative<m::VideoFrame>(*frame));const auto& value=std::get<m::VideoFrame>(*frame);
            CHECK(value.width==int(info.width) && value.height==int(info.height) && value.time_us>=previous && std::chrono::steady_clock::now()<until);previous=value.time_us;
            if(video_count<prefix){CHECK(stamp(*frame)==video_reference[video_count]);++comparisons;}
            if(!held){held=value;held_pixel=value.planes[0].pixels[0];}++video_count;if(!full && video_count==prefix)break;
        }
        CHECK(video_count>=prefix && !reader.statistics().audio_frames);bounded(reader.statistics());video_bytes=reader.statistics().bytes_read;
        if(full)CHECK(!reader.next() && !reader.next(m::Track::video));
    }
    CHECK(held && held->owner && held->planes[0].pixels && held->planes[0].pixels[0]==held_pixel);
    {
        m::Reader reader(source,{},m::Track::audio);std::int64_t previous=-1;
        while(auto frame=reader.next()){
            CHECK(std::holds_alternative<m::AudioFrame>(*frame));const auto& value=std::get<m::AudioFrame>(*frame);
            CHECK(value.rate==info.audio_rate && value.channels==info.audio_channels && value.time_us>=previous && !value.pcm16.empty() && value.pcm16.size()%4==0 && std::chrono::steady_clock::now()<until);previous=value.time_us;
            if(audio_count<prefix){CHECK(stamp(*frame)==audio_reference[audio_count]);++comparisons;}
            samples+=value.pcm16.size()/4;CHECK(++audio_count<200000);
        }
        CHECK(audio_count>=prefix && !reader.statistics().video_frames && !reader.next() && !reader.next(m::Track::audio));bounded(reader.statistics());audio_bytes=reader.statistics().bytes_read;
        rejects([&]{reader.next(m::Track::video);},"excluded at construction");
    }
    unsupported(source,false);unsupported(source,true);
    rejects([&]{m::Reader reader(source,{},m::Track(99));},"invalid fixed movie track");
    rejects([&]{m::Reader reader(std::shared_ptr<r::Source>{});},"missing original movie source");
    for(auto track:{m::Track::video,m::Track::audio})rejects([&]{m::Reader reader(source,[]{return true;},track);});
    CHECK(comparisons==prefix*2 && guards==7);
    std::ofstream report(std::filesystem::u8path(argv[2]));report<<"{\"schema\":\"wa2.movie-codecs.v1\",\"source_bytes\":"<<source->size()<<",\"checks\":"<<checks<<",\"guards\":"<<guards<<",\"width\":"<<info.width<<",\"height\":"<<info.height<<",\"audio_rate\":"<<info.audio_rate<<",\"audio_channels\":"<<info.audio_channels<<",\"duration_us\":"<<info.duration_us<<",\"exact_pcm_yuv_prefixes\":"<<comparisons<<",\"video_frames\":"<<video_count<<",\"audio_frames\":"<<audio_count<<",\"audio_samples\":"<<samples<<",\"video_bytes_read\":"<<video_bytes<<",\"audio_bytes_read\":"<<audio_bytes<<",\"video_to_eof\":"<<(full?"true":"false")<<",\"audio_to_eof\":true,\"zero_cross_track_backlog\":true,\"unsupported_codecs_explicit\":true,\"game_launched\":false,\"screen_access\":false,\"devices_opened\":false,\"passed\":true}\n";CHECK(report.good());
    std::cout<<"PASS "<<std::filesystem::u8path(argv[1]).filename().string()<<": "<<video_count<<" video / "<<audio_count<<" audio frames; "<<comparisons<<" exact PCM/YUV prefixes.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
