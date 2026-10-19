#define SDL_MAIN_HANDLED
#include "movie_reader.hpp"
#include "movie_player.hpp"
#include "scene_movie.hpp"
#include "scene_controls.hpp"
#include "scene_music.hpp"
#include "scene_effects.hpp"
#include "scene_weather.hpp"
#include "scene_dialogue.hpp"
#include "scene_colour.hpp"
#include "scene_bitmaps.hpp"
#include "resource_catalog.hpp"
#include "script_session.hpp"
#include "sdl_movie.hpp"
#include "sdl_scene.hpp"
#include "file_io.hpp"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <thread>

namespace m=wa2::movie;namespace s=wa2::script;namespace r=wa2::resource;namespace p=wa2::platform;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
unsigned reader_cases{},render_cases{},control_cases{},player_cases{};
std::uint64_t read_bytes{},max_read{},decoded_audio{},decoded_video{},peak_video{},peak_audio{},peak_packets{};
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
struct BadSource:r::Source{
    std::uint64_t size()const override{return 100000;}
    void read_at(std::uint64_t,std::span<std::uint8_t>)override{throw r::Error("deliberate test I/O failure");}
};
struct Fake final:m::Playback{
    bool done{},stopped{};
    bool finished()const override{return done;}
    void stop()override{done=stopped=true;}
    void update(std::int64_t)override{}
    void pause(bool,std::int64_t)override{}
    void gain(float)override{}
    std::optional<m::VideoFrame> frame()const override{return {};}
};
void controls(){
    const std::string initializers="4,v,-1.25\r\n4,z,-0.0\r\n4,e,1.5e2\r\n";
    const auto globals=s::Globals::parse({reinterpret_cast<const std::uint8_t*>(initializers.data()),initializers.size()});
    CHECK(globals.get(0).real()==-1.25f && globals.get(1).real()==0.f && std::signbit(globals.get(1).real()) && globals.get(2).real()==150.f);
    ++control_cases;
    for(const std::string invalid:{"4,v,xyz","4,v,1e1000","4,v,+1.0"}){
        rejects([&]{s::Globals::parse({reinterpret_cast<const std::uint8_t*>(invalid.data()),invalid.size()});});++control_cases;
    }
    unsigned begins=0,prepares=0;std::string name;std::int32_t mode{};Fake* active{};
    s::SceneMovie movies([&](auto file,auto skip){++begins;name=file;mode=skip;auto result=std::make_unique<Fake>();active=result.get();return result;},[&]{++prepares;});
    const std::array<s::Value,2> args{s::Value(std::string("mv00")),s::Value(1)};
    s::Command command{193,args};CHECK(!movies.dispatch(command)->complete && command.continuation==1);
    CHECK(name=="mv000.pak" && mode==1 && begins==1 && prepares==1 && movies.active());++control_cases;
    CHECK(!movies.dispatch(command)->complete && begins==1);active->done=true;
    CHECK(movies.dispatch(command)->complete && !movies.active() && !movies.playback());++control_cases;
    command={193,args};movies.dispatch(command);movies.stop();CHECK(active->stopped && movies.dispatch(command)->complete);++control_cases;
    const std::array<s::Value,2> invalid{s::Value(std::string("../mv00")),s::Value(1)};
    command={193,invalid};rejects([&]{movies.dispatch(command);});++control_cases;
    command={194,args};CHECK(!movies.dispatch(command));++control_cases;
    std::uint32_t clock=0;s::SceneControls scene([&]{return clock;});command={207,{}};const auto draw=scene.dispatch(command);
    CHECK(draw && draw->complete && draw->yield_frame && scene.take_draw_request() && !scene.take_draw_request());++control_cases;
    s::SceneMovie alternate([&](auto file,auto){name=file;return std::make_unique<Fake>();},{},2);
    command={193,args};alternate.dispatch(command);CHECK(name=="mv002.pak");++control_cases;
}
m::VideoFrame tiny(std::uint8_t brightness){
    auto bytes=std::make_shared<std::array<std::uint8_t,6>>();bytes->fill(brightness);(*bytes)[4]=(*bytes)[5]=128;
    m::VideoFrame frame;frame.owner=bytes;frame.width=frame.height=2;
    frame.planes={m::Plane{bytes->data(),2,2,2},m::Plane{bytes->data()+4,1,1,1},m::Plane{bytes->data()+5,1,1,1}};return frame;
}
std::array<std::uint8_t,4> pixel(SDL_Surface* surface,int x,int y){
    Uint32 value{};std::memcpy(&value,static_cast<const std::uint8_t*>(surface->pixels)+y*surface->pitch+x*4,4);
    std::array<std::uint8_t,4> out{};SDL_GetRGBA(value,surface->format,&out[0],&out[1],&out[2],&out[3]);return out;
}
void rendering(p::SdlCanvas& canvas,SDL_Surface* surface){
    p::SdlMovie display(canvas);const auto mode=SDL_GetYUVConversionMode();auto frame=tiny(235);
    display.draw(frame,32,18);canvas.present();auto value=pixel(surface,16,9);
    CHECK(value[0]>=253 && value[1]>=253 && value[2]>=253 && value[3]==255);
    CHECK(pixel(surface,0,9)[0]==0 && SDL_GetYUVConversionMode()==mode);++render_cases;
    auto uploads=canvas.statistics().uploads;display.draw(frame,32,18);CHECK(canvas.statistics().uploads==uploads);++render_cases;
    frame=tiny(16);display.draw(frame,32,18);canvas.present();value=pixel(surface,16,9);
    CHECK(value[0]<=2 && value[1]<=2 && value[2]<=2 && canvas.statistics().uploads==uploads+1);++render_cases;
    display.reset();display.draw(frame,32,18);CHECK(canvas.statistics().uploads==uploads+2);++render_cases;
    auto bad=frame;bad.planes[1].pitch=0;rejects([&]{canvas.upload_yuv(bad);});++render_cases;
}
void readers(const std::filesystem::path& directory,p::SdlCanvas& canvas){
    for(const auto filename:{"mv000.pak","mv100.pak"}){
        std::optional<m::VideoFrame> held;
        {
            auto source=p::open_resource_file(directory/filename);m::Reader reader(source);const auto info=reader.information();
            CHECK(info.width==1280 && info.height==720 && info.audio_channels==2 && info.audio_rate>=44100);++reader_cases;
            unsigned video=0,audio=0;std::int64_t last_video=-1,last_audio=-1,first_video=-1,first_audio=-1;
            while(video<8 || audio<8){
                auto frame=reader.next(audio<8?m::Track::audio:m::Track::video);CHECK(frame);
                if(auto* value=std::get_if<m::VideoFrame>(&*frame)){
                    CHECK(value->owner && value->width==1280 && value->height==720 && value->time_us>=last_video && value->duration_us>0);
                    last_video=value->time_us;if(first_video<0)first_video=last_video;++video;if(!held)held=*value;
                }else{
                    const auto& samples=std::get<m::AudioFrame>(*frame);CHECK(samples.channels==2 && samples.rate==info.audio_rate &&
                        !samples.pcm16.empty() && samples.pcm16.size()%4==0 && samples.time_us>=last_audio);last_audio=samples.time_us;
                    if(first_audio<0)first_audio=last_audio;++audio;
                }
                CHECK(video+audio<1000);
            }
            const auto stats=reader.statistics();CHECK(stats.video_frames==8 && stats.audio_frames==8 && stats.largest_read<=32768);
            CHECK(stats.peak_packet_bytes>0 && stats.peak_packet_bytes<=8*1024*1024 && stats.peak_packet_count<=2048);
            CHECK(stats.bytes_read<source->size());read_bytes+=stats.bytes_read;max_read=std::max(max_read,std::uint64_t(stats.largest_read));
            decoded_audio+=stats.audio_frames;decoded_video+=stats.video_frames;peak_packets=std::max(peak_packets,std::uint64_t(stats.peak_packet_bytes));++reader_cases;
            std::cout<<filename<<": "<<info.width<<'x'<<info.height<<", "<<info.audio_rate<<" Hz, "<<stats.video_frames
                <<" video / "<<stats.audio_frames<<" audio frames, "<<stats.bytes_read<<" bytes read, duration "<<info.duration_us
                <<" us, first video/audio "<<first_video<<'/'<<first_audio<<" us, peak compressed queue "<<stats.peak_packet_bytes<<std::endl;
        }
        // Pixel buffers remain valid after the decoder and input descriptor close.
        CHECK(held && held->planes[0].pixels);p::SdlMovie display(canvas);display.draw(*held,32,18);canvas.present();++reader_cases;
    }
    rejects([&]{m::Reader reader(std::make_shared<BadSource>());});++reader_cases;
    rejects([&]{m::Reader reader(p::open_resource_file(directory/"mv000.pak"),[]{return true;});});++reader_cases;
}
void players(const std::filesystem::path& directory){
    auto mixer=std::make_shared<wa2::audio::Mixer>();m::Player player(p::open_resource_file(directory/"mv000.pak"),mixer);player.gain(1);
    std::vector<float> out(1600);std::int64_t clock=0;bool saw_video=false,paused_once=false;unsigned iterations=0;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(!player.finished() && std::chrono::steady_clock::now()<deadline){
        player.update(clock);mixer->render(out,48000);clock+=16667;
        const auto stats=player.statistics();peak_video=std::max(peak_video,std::uint64_t(stats.peak_video_frames));
        peak_audio=std::max(peak_audio,std::uint64_t(stats.queued_audio_frames));CHECK(stats.queued_video_frames<=4 && stats.queued_audio_frames<=16384);
        if(player.frame())saw_video=true;
        if(saw_video && !paused_once){
            player.pause(true,clock);const auto frozen=player.frame()->owner;
            player.update(clock+1000000);mixer->render(out,48000);
            CHECK(player.frame()->owner==frozen && std::all_of(out.begin(),out.end(),[](float sample){return sample==0;}));
            player.pause(false,clock+1000000);clock+=1000000;paused_once=true;++player_cases;
        }
        // Six ordinary logic/audio ticks per sleep avoid making a functional
        // unit test wait for Windows' roughly 16 ms sleep quantum each tick.
        if(++iterations%6==0)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if(!saw_video || !player.finished() || !player.statistics().decode_finished){const auto stats=player.statistics();
        std::cerr<<"Player timeout: simulated clock="<<clock<<", decoded="<<stats.reader.video_frames<<'/'<<stats.reader.audio_frames
            <<", queued="<<stats.queued_video_frames<<'/'<<stats.queued_audio_frames<<", eof="<<stats.decode_finished
            <<", current video="<<(player.frame()?player.frame()->time_us:-1)<<'\n';}
    CHECK(saw_video && player.finished() && player.statistics().decode_finished);++player_cases;
    CHECK(peak_video<=4 && peak_audio<=16384);++player_cases;
    m::Player cancelled(p::open_resource_file(directory/"mv100.pak"),mixer);cancelled.stop();CHECK(cancelled.finished());++player_cases;
    m::Player failure(std::make_shared<BadSource>(),mixer);bool failed=false;
    for(unsigned n=0;n<100 && !failed;++n){try{failure.update(n);}catch(const std::exception&){failed=true;}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    CHECK(failed);++player_cases;
}
int main(int argc,char** argv){try{
    CHECK(argc==3);const auto directory=std::filesystem::path(argv[1]);controls();CHECK(SDL_Init(0)==0);
    std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)> surface(SDL_CreateRGBSurfaceWithFormat(0,32,18,32,SDL_PIXELFORMAT_RGBA32),SDL_FreeSurface);
    CHECK(surface);
    std::uint32_t next=0;std::string next_name,actual_movie,boundary_reason;std::uint64_t scene_video{},scene_audio{};bool scene_finished=false;unsigned story_messages{};
    {
        auto canvas=p::SdlCanvas::adopt(SDL_CreateSoftwareRenderer(surface.get()));rendering(*canvas,surface.get());readers(directory,*canvas);players(directory);
        canvas->logical_size(1280,720);p::SdlScene background_display(*canvas);p::SdlMovie movie_display(*canvas);
        auto mixer=std::make_shared<wa2::audio::Mixer>();
        // Scene 1001 is introductory chapter; its artwork/audio/movie banks
        // belong to IC, while the translated script catalogue is shared.
        const auto chapter=directory/"IC";
        wa2::image::ImageStore images(r::Archive::open(p::open_resource_file(chapter/"bak.pak")));
        wa2::image::ImageStore interface_images(r::Archive::open(p::open_resource_file(chapter/"grp.pak")));
        wa2::image::ImageStore system_images(r::Archive::open(p::open_resource_file(directory/"grp.pak")));
        wa2::text::FontBook fonts(r::Archive::open(p::open_resource_file(directory/"fon.pak")));
        s::SceneDialogue dialogue(fonts,[&](auto name){return system_images.request(name);},[&](auto id){++story_messages;std::cout<<"Original story message "<<id<<" rendered natively"<<std::endl;});
        s::SceneColour colour;
        r::Catalog resources([&](auto name)->std::shared_ptr<r::Source>{const auto path=directory/std::string(name);return std::filesystem::exists(path)?p::open_resource_file(path):nullptr;});
        s::SceneBitmaps bitmaps([&](auto name,auto bank,auto){return resources.image(bank,name);});
        s::SceneBackground backgrounds([&](auto name){return images.request(name);},{},[&](auto name){return interface_images.request(name);});
        wa2::Random random(1);s::SceneWeather weather([&](auto name){return interface_images.request(name);},[&]{return random.next();});
        s::SceneMovie movies([&](auto file,auto){actual_movie=file;auto source=p::open_resource_file(chapter/file);
            {m::Reader header(source);std::cout<<"Scene requested movie IC/"<<file<<", duration "<<header.information().duration_us<<" us"<<std::endl;}
            auto result=std::make_unique<m::Player>(std::move(source),mixer);result->gain(1);return result;});
        std::uint32_t milliseconds=100;s::SceneControls controls([&]{return milliseconds;});
        std::optional<r::Archive> music_bank;
        s::SceneMusic music([&](auto song,bool loop){
            if(!music_bank)music_bank=r::Archive::open(p::open_resource_file(chapter/"BGM.PAK"));
            return wa2::audio::play_ogg(*mixer,wa2::audio::music_playlist(*music_bank,song,loop));
        });
        std::optional<r::Archive> effects_bank;
        s::SceneEffects effects([&](auto sound,bool loop){
            if(!effects_bank)effects_bank=r::Archive::open(p::open_resource_file(chapter/"SE.PAK"));
            std::array<char,32> name{};std::snprintf(name.data(),name.size(),"SE_%04d.ogg",int(sound));
            auto member=effects_bank->find_first(name.data());
            if(member!=r::Archive::missing){auto source=effects_bank->open_member(member);return wa2::audio::play_ogg(*mixer,{source,loop?source:nullptr});}
            std::snprintf(name.data(),name.size(),"SE_%04d.wav",int(sound));member=effects_bank->find_first(name.data());
            if(member==r::Archive::missing)throw s::Error("missing original effect "+std::string(name.data()));
            return wa2::audio::play_wave(*mixer,effects_bank->open_member(member),loop);
        });
        s::Library library(r::Archive::open(p::open_resource_file(directory/"ck-gal.pak")));s::Session session(library);session.start("1001");
        const auto host=[&](s::Command& command){
            try{
            if(auto value=controls.dispatch(command))return *value;
            if(auto value=music.dispatch(command))return *value;
            if(auto value=effects.dispatch(command))return *value;
            if(auto value=weather.dispatch(command))return *value;
            if(auto value=dialogue.dispatch(command))return *value;
            if(auto value=colour.dispatch(command))return *value;
            if(auto value=bitmaps.dispatch(command))return *value;
            if(auto value=backgrounds.dispatch(command,controls.modes().time))return *value;
            if(auto value=movies.dispatch(command))return *value;
            }catch(const s::Unsupported& error){boundary_reason=error.what();}
                std::cout<<"Unimplemented command "<<command.index<<" parameters:";
                for(const auto& value:command.arguments){
                    if(value.type()==s::Type::text){const auto& text=value.text();if(text.size()<64 && std::all_of(text.begin(),text.end(),[](unsigned char c){return c>=32 && c<127;}))std::cout<<' '<<std::quoted(text);else std::cout<<" <text>";}
                    else if(value.type()==s::Type::real)std::cout<<' '<<value.real();
                    else std::cout<<' '<<value.integer();
                }
                std::cout<<std::endl;
            next=command.index;next_name=library.functions().at(next).name;return s::CommandResult{false,true,{}};
        };
        bool presented_movie=false;std::vector<float> out(1600);unsigned steps=0;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while(!next && !scene_finished && std::chrono::steady_clock::now()<deadline){
            milliseconds=100+steps*1000/60;movies.update(std::int64_t(steps)*1000000/60);backgrounds.update();weather.update();music.update();effects.update();dialogue.update();colour.update();bitmaps.update();
            if(dialogue.phase()==s::DialoguePhase::waiting){background_display.sprites(dialogue.sprites());dialogue.advance();}
            if(const auto* player=dynamic_cast<const m::Player*>(movies.playback())){const auto stats=player->statistics();
                scene_video=stats.reader.video_frames;scene_audio=stats.reader.audio_frames;}
            if(!presented_movie)if(auto frame=movies.frame()){movie_display.draw(*frame);canvas->present();presented_movie=true;}
            scene_finished=session.advance(host)==s::RunState::stopped;mixer->render(out,48000);++steps;
            if(steps%6==0)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if(!next && !scene_finished){std::cerr<<"Scene timeout: steps="<<steps<<", presented="<<presented_movie<<", decoded="<<scene_video<<'/'<<scene_audio;
            if(const auto* player=dynamic_cast<const m::Player*>(movies.playback())){const auto stats=player->statistics();
                std::cerr<<", queued="<<stats.queued_video_frames<<'/'<<stats.queued_audio_frames<<", eof="<<stats.decode_finished
                    <<", current video="<<(player->frame()?player->frame()->time_us:-1);}
            std::cerr<<'\n';}
        CHECK(presented_movie && (next || scene_finished) && next!=193 && actual_movie=="mv010.pak" && scene_video && scene_audio);
        std::cout<<"Actual scene completed movie "<<actual_movie<<"; next command "<<next<<' '<<next_name<<' '<<boundary_reason<<'\n';
    }
    surface.reset();SDL_Quit();
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.native-movie.v1\",\"reader_cases\":"<<reader_cases
        <<",\"render_cases\":"<<render_cases<<",\"control_cases\":"<<control_cases<<",\"player_cases\":"<<player_cases
        <<",\"prefix_bytes_read\":"<<read_bytes<<",\"largest_source_read\":"<<max_read<<",\"prefix_video_frames\":"<<decoded_video
        <<",\"prefix_audio_frames\":"<<decoded_audio<<",\"peak_video_queue\":"<<peak_video<<",\"peak_audio_queue\":"<<peak_audio
        <<",\"peak_prefix_compressed_packet_bytes\":"<<peak_packets
        <<",\"scene_movie_video_frames\":"<<scene_video<<",\"scene_movie_audio_frames\":"<<scene_audio
        <<",\"actual_scene_movie\":\""<<actual_movie<<"\",\"next_unimplemented_command\":"<<next
        <<",\"native_story_messages\":"<<story_messages
        <<",\"next_unimplemented_command_name\":\""<<next_name<<"\",\"boundary_reason\":\""<<boundary_reason<<"\",\"screen_access\":false,\"windows_created\":0,"
        "\"audio_devices_opened\":0,\"game_launched\":false,\"cpu_emulator_linked\":false,\"resources_transcoded\":false,"
        "\"resources_in_apk\":false,\"complete_gameplay_verified\":false,\"apk_created\":false,\"passed\":true}\n";
    std::cout<<"PASS native movie: "<<reader_cases<<" reader, "<<render_cases<<" render, "<<control_cases<<" scene-control and "<<player_cases<<" player checks\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
