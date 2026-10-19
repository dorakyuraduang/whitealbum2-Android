// Real decoder packets and PCM/YUV remain in owned memory. No game, window,
// renderer, emulator or audio device is created; the external movie stays put.
#include "movie_reader.hpp"
#include "movie_player.hpp"
#include "pcm_audio.hpp"
#include "../platform/file_io.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace m=wa2::movie;
constexpr unsigned Prefix=16;
struct Stamp {
    std::int64_t time{},duration{};unsigned width{},height{},rate{},channels{};std::uint64_t hash{1469598103934665603ull};
    bool operator==(const Stamp&)const=default;
    void bytes(std::span<const std::uint8_t> data){for(auto value:data){hash^=value;hash*=1099511628211ull;}}
};
Stamp stamp(const m::Frame& frame){
    Stamp result;
    if(const auto* video=std::get_if<m::VideoFrame>(&frame)){
        CHECK(video->owner);result.time=video->time_us;result.duration=video->duration_us;result.width=video->width;result.height=video->height;
        for(const auto& plane:video->planes)for(int y=0;y<plane.height;++y)result.bytes({plane.pixels+std::size_t(y)*plane.pitch,std::size_t(plane.width)});
    }else{
        const auto& audio=std::get<m::AudioFrame>(frame);result.time=audio.time_us;result.rate=audio.rate;result.channels=audio.channels;result.bytes(audio.pcm16);
    }
    return result;
}
void bounded(const m::ReaderStatistics& stats){CHECK(!stats.queued_packet_bytes && !stats.peak_packet_bytes && !stats.peak_packet_count && stats.largest_read<=32768);}
int main(int argc,char** argv){try{
    CHECK(argc==3);auto source=wa2::platform::open_resource_file(argv[1]);CHECK(source->size()>16u*1024*1024);
    std::vector<Stamp> video_reference,audio_reference;
    m::Information expected;
    {
        m::Reader sequential(source);expected=sequential.information();CHECK(expected.audio_rate && expected.width && expected.height);
        unsigned decoded{};
        while(video_reference.size()<Prefix || audio_reference.size()<Prefix){
            auto frame=sequential.next();CHECK(frame && ++decoded<=4096);
            auto& target=std::holds_alternative<m::VideoFrame>(*frame)?video_reference:audio_reference;
            if(target.size()<Prefix)target.push_back(stamp(*frame));
        }
        bounded(sequential.statistics());
    }
    unsigned prefix_checks{},rejections{};std::uint64_t audio_frames{},audio_bytes{},video_bytes{};
    // The old shared cursor overflows while trying to find the last audio
    // packet, even when a caller can consume audio and video at different rates.
    // Draining audio first now retains no video packet or decoded picture.
    m::Reader audio(source,{},m::Track::audio);
    CHECK(audio.information().width==expected.width && audio.information().height==expected.height && audio.information().audio_rate==expected.audio_rate);
    std::int64_t last=-1;
    while(auto frame=audio.next()){
        CHECK(std::holds_alternative<m::AudioFrame>(*frame));const auto value=stamp(*frame);CHECK(value.time>=last);last=value.time;
        if(audio_frames<Prefix){CHECK(value==audio_reference[audio_frames]);++prefix_checks;}
        CHECK(++audio_frames<=20000);bounded(audio.statistics());
    }
    const auto audio_stats=audio.statistics();CHECK(audio_stats.audio_frames==audio_frames && !audio_stats.video_frames && audio_stats.bytes_read>8u*1024*1024);
    CHECK(audio_frames>Prefix && !audio.next() && !audio.next(m::Track::audio));audio_bytes=audio_stats.bytes_read;
    {
        m::Reader video(source,{},m::Track::video);
        bool rejected{};try{video.next(m::Track::audio);}catch(const m::Error&){rejected=true;}CHECK(rejected);++rejections;
        for(unsigned n=0;n<Prefix;++n){auto frame=video.next();CHECK(frame && std::holds_alternative<m::VideoFrame>(*frame));CHECK(stamp(*frame)==video_reference[n]);++prefix_checks;}
        CHECK(video.statistics().video_frames==Prefix && !video.statistics().audio_frames);bounded(video.statistics());video_bytes=video.statistics().bytes_read;
    }
    // Interleaving calls on two readers also exercises the same seekable Source
    // with independent buffered file positions, rather than separate files.
    {
        m::Reader video(source,{},m::Track::video),voice(source,{},m::Track::audio);
        for(unsigned n=0;n<Prefix;++n){
            auto v=video.next(m::Track::video);CHECK(v && stamp(*v)==video_reference[n]);++prefix_checks;
            auto a=voice.next(m::Track::audio);CHECK(a && stamp(*a)==audio_reference[n]);++prefix_checks;
        }
        bounded(video.statistics());bounded(voice.statistics());
    }
    for(unsigned variant=0;variant<2;++variant){bool rejected{};
        try{m::Reader reader(source,[]{return true;},variant?m::Track::audio:m::Track::video);}
        catch(const m::Error&){rejected=true;}CHECK(rejected);++rejections;
    }
    {
        bool rejected{};try{m::Reader reader(source,{},m::Track(99));}catch(const m::Error&){rejected=true;}CHECK(rejected);++rejections;
    }
    m::PlayerStatistics player_stats;
    {
        auto mixer=std::make_shared<wa2::audio::Mixer>();m::Player player(source,mixer);std::vector<float> pcm(1600);std::int64_t clock{};
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);bool ready{};
        while(std::chrono::steady_clock::now()<until){
            player.update(clock);mixer->render(pcm,48000);clock+=16667;player_stats=player.statistics();
            bounded(player_stats.reader);CHECK(player_stats.queued_video_frames<=4 && player_stats.queued_audio_frames<=16384);
            if(player.frame() && player_stats.reader.video_frames>=8 && player_stats.reader.audio_frames>=8){ready=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(ready);player.stop();CHECK(player.finished());
    }
    std::ofstream out(argv[2]);out<<"{\"schema\":\"wa2.bounded-movie-tracks.v1\",\"source_bytes\":"<<source->size()
        <<",\"audio_frames_to_eof\":"<<audio_frames<<",\"audio_cursor_bytes_read\":"<<audio_bytes<<",\"video_prefix_bytes_read\":"<<video_bytes
        <<",\"exact_pcm_yuv_timestamp_prefixes\":"<<prefix_checks<<",\"explicit_rejections\":"<<rejections
        <<",\"player_video_frames\":"<<player_stats.reader.video_frames<<",\"player_audio_frames\":"<<player_stats.reader.audio_frames
        <<",\"peak_video_frames\":"<<player_stats.peak_video_frames<<",\"peak_audio_frames\":"<<player_stats.queued_audio_frames
        <<",\"compressed_backlog_bytes\":0,\"same_source_independent_positions\":true,\"audio_eof_does_not_consume_video\":true"
        <<",\"bounded_io_reads\":true,\"original_resources_external\":true,\"game_launched\":false,\"screen_access\":false,\"audio_device_opened\":false,\"passed\":true}\n";
    CHECK(out.good());std::cout<<"PASS movie track isolation: "<<audio_frames<<" audio frames to EOF, "<<prefix_checks<<" exact prefixes, zero cross-track backlog; no game/window/device.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
