// Own-memory codec and recovery checks. No game, window or device is started.
#include "../engine/movie_player.hpp"
#include "../platform/file_io.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

namespace m=wa2::movie;namespace r=wa2::resource;
unsigned checks{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": "+#x);}while(false)
struct FaultSource final:r::Source {
    std::shared_ptr<r::Source> base;std::atomic<bool> fail{};
    std::uint64_t size()const override{return base->size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> bytes)override{if(fail.load())throw r::Error("owned mid-movie I/O fault");base->read_at(at,bytes);}
};
struct BrokenSource final:r::Source {
    std::uint64_t size()const override{return 64;}
    void read_at(std::uint64_t at,std::span<std::uint8_t> bytes)override{CHECK(at<=64 && bytes.size()<=64-at);std::fill(bytes.begin(),bytes.end(),0x39);}
};
int main(int argc,char** argv){try{
    CHECK(argc>=4);unsigned files{},video_frames{},audio_frames{},recoveries{};bool jpeg{},aac{},renamed{},video_only{},surround{};
    for(int n=3;n<argc;++n){
        const auto path=std::filesystem::u8path(argv[n]);auto source=wa2::platform::open_resource_file(path);m::Reader reader(source);const auto info=reader.information();
        CHECK(info.width>0 && info.height>0);unsigned video{},audio{};std::int64_t last_video=-1,last_audio=-1;
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while(auto frame=reader.next()){
            CHECK(std::chrono::steady_clock::now()<until && video+audio<100000);
            if(const auto* picture=std::get_if<m::VideoFrame>(&*frame)){
                CHECK(picture->owner && picture->width>0 && picture->height>0 && picture->time_us>=last_video && picture->duration_us>0);last_video=picture->time_us;
                for(const auto& plane:picture->planes)CHECK(plane.pixels && plane.pitch>=plane.width && plane.width>0 && plane.height>0);
                if(!video && path.filename().string().starts_with("own-h264")){CHECK(picture->width==16 && picture->height==16 && picture->planes[0].pixels[0]==64);}
                if(path.extension()==".avi"){CHECK(picture->full_range);jpeg=true;}++video;
            }else{
                const auto& sound=std::get<m::AudioFrame>(*frame);CHECK(sound.channels==info.audio_channels && sound.channels>=1 && sound.channels<=2 && sound.rate==info.audio_rate && sound.time_us>=last_audio && !sound.pcm16.empty() && sound.pcm16.size()%(2*sound.channels)==0);last_audio=sound.time_us;
                if(path.filename().string().starts_with("own-h264")){CHECK(std::all_of(sound.pcm16.begin(),sound.pcm16.end(),[](auto value){return value==0;}));aac=true;}
                ++audio;
            }
        }
        CHECK(video>0 && (info.audio_rate?audio>0:audio==0) && !reader.next());const auto stats=reader.statistics();CHECK(stats.largest_read<=32768 && stats.peak_packet_bytes==0);
        if(path.filename()=="own-mjpeg-surround.avi"){CHECK(info.audio_channels==2 && audio>0);surround=true;}
        if(path.extension()==".pak")renamed=true;if(!info.audio_rate)video_only=true;
        video_frames+=video;audio_frames+=audio;++files;std::cout<<"PASS "<<path.filename().string()<<": "<<video<<" video / "<<audio<<" audio frames.\n";
    }
    auto mixer=std::make_shared<wa2::audio::Mixer>();std::vector<float> output(1600);auto now=std::int64_t{};
    {
        m::Player player(std::make_shared<BrokenSource>(),mixer);
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!player.finished() && std::chrono::steady_clock::now()<until){player.update(now+=16667);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        CHECK(player.finished() && !player.failure().empty() && !player.frame());mixer->render(output,48000);CHECK(std::all_of(output.begin(),output.end(),[](auto value){return value==0;}));++recoveries;
    }
    {
        auto source=std::make_shared<FaultSource>();source->base=wa2::platform::open_resource_file(std::filesystem::u8path(argv[2]));m::Player player(source,mixer);player.gain(1);
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);bool started{};
        while(!player.finished() && std::chrono::steady_clock::now()<until){
            player.update(now+=16667);mixer->render(output,48000);if(player.frame()){started=true;source->fail.store(true);}std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(started && player.finished() && player.failure().find("owned mid-movie I/O fault")!=std::string::npos && !player.frame());
        CHECK(player.statistics().queued_video_frames==0 && player.statistics().queued_audio_frames==0);mixer->render(output,48000);CHECK(std::all_of(output.begin(),output.end(),[](auto value){return value==0;}));++recoveries;
    }
    {
        m::Player player(wa2::platform::open_resource_file(std::filesystem::u8path(argv[2])),mixer,[]{throw std::logic_error("owned non-media worker fault");});
        bool caught{};const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!caught && std::chrono::steady_clock::now()<until){try{player.update(now+=16667);}catch(const std::logic_error& error){caught=std::string_view(error.what())=="owned non-media worker fault";}std::this_thread::sleep_for(std::chrono::milliseconds(1));}CHECK(caught);
    }
    CHECK(jpeg && aac && renamed && surround && files>=6 && recoveries==2);
    std::ofstream report(std::filesystem::u8path(argv[1]));report<<"{\"schema\":\"wa2.movie-formats.v1\",\"checks\":"<<checks<<",\"files\":"<<files<<",\"video_frames\":"<<video_frames<<",\"audio_frames\":"<<audio_frames<<",\"recoveries\":"<<recoveries<<",\"full_decode_to_eof\":true,\"content_detection_for_pak\":true,\"jpeg_pixel_conversion\":true,\"aac_and_packed_pcm\":true,\"surround_to_stereo\":true,\"video_only\":"<<(video_only?"true":"false")<<",\"failed_movie_stops_audio_and_clears_frames\":true,\"non_media_exceptions_preserved\":true,\"game_launched\":false,\"screen_access\":false,\"devices_opened\":false,\"passed\":true}\n";CHECK(report.good());return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
