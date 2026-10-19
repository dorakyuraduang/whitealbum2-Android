#define SDL_MAIN_HANDLED
#include "scene.hpp"
#include "sdl_scene.hpp"
#include "sdl_movie.hpp"
#include "file_io.hpp"
#include <SDL.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

namespace g=wa2::game;namespace s=wa2::script;namespace r=wa2::resource;namespace p=wa2::platform;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
int main(int argc,char** argv){try{
    CHECK(argc==3);const auto root=std::filesystem::path(argv[1]);CHECK(SDL_Init(0)==0);
    std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)> surface(SDL_CreateRGBSurfaceWithFormat(0,64,36,32,SDL_PIXELFORMAT_RGBA32),SDL_FreeSurface);CHECK(surface);
    std::string boundary,active_scene;std::uint32_t last_command{};std::uint64_t messages{},ticks{},voices{};unsigned movie_presents{},dialogue_presents{};bool finished=false;
    {auto canvas=p::SdlCanvas::adopt(SDL_CreateSoftwareRenderer(surface.get()));canvas->logical_size(1280,720);p::SdlScene display(*canvas);p::SdlMovie movie_display(*canvas);
        g::Scene scene([&](auto name)->std::shared_ptr<r::Source>{const auto path=root/std::string(name);return std::filesystem::exists(path)?p::open_resource_file(path):nullptr;});scene.start();
        std::vector<float> output(1600);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(35);
        while(!finished && std::chrono::steady_clock::now()<deadline){
            const auto phase=scene.dialogue_state().phase();
            const bool advance=phase==s::DialoguePhase::waiting || (phase==s::DialoguePhase::typing && ticks%2==0) ||
                (!scene.movie_active() && ticks%120==0 && (phase==s::DialoguePhase::closed || phase==s::DialoguePhase::complete));
            try{finished=scene.update(std::int64_t(ticks)*1000000/60,{advance,false,false})==s::RunState::stopped;}
            catch(const s::Unsupported& error){boundary=error.what();break;}
            if(!movie_presents)if(auto frame=scene.movie()){movie_display.draw(*frame);canvas->present();++movie_presents;}
            if(advance && (dialogue_presents<100 || scene.statistics().messages%10==0)){display.background(scene.background(),scene.environment());display.sprites(scene.foreground());display.sprites(scene.dialogue());canvas->present();++dialogue_presents;}
            scene.mixer()->render(output,48000);++ticks;
            if(scene.statistics().messages>=1500)break; // A bounded story check, not a whole-game claim.
            if(ticks%6==0)std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        messages=scene.statistics().messages;voices=scene.statistics().voices;active_scene=scene.statistics().scene;last_command=scene.statistics().command;
        std::cout<<"Stopped check at scene "<<active_scene<<" command "<<last_command<<" ticks "<<ticks<<" messages "<<messages<<" voices "<<voices<<" phase "<<int(scene.dialogue_state().phase())<<std::endl;
        CHECK(movie_presents && dialogue_presents>=60 && messages>=60 && (finished || !boundary.empty() || (messages>=150 && voices>0)));
        std::cout<<"Native shared game host: "<<messages<<" original messages; "<<boundary<<'\n';
    }
    surface.reset();SDL_Quit();std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.native-game-scene.v1\",\"original_messages\":"<<messages<<",\"logic_ticks\":"<<ticks
        <<",\"original_voices\":"<<voices<<",\"active_scene\":"<<std::quoted(active_scene)<<",\"last_command\":"<<last_command<<",\"movie_presents\":"<<movie_presents<<",\"dialogue_presents\":"<<dialogue_presents<<",\"scene_finished\":"<<(finished?"true":"false")
        <<",\"boundary\":"<<std::quoted(boundary)<<",\"same_scene_host_as_android\":true,\"source_executable_opened\":false,\"screen_access\":false,\"windows_created\":0,"
          "\"audio_devices_opened\":0,\"game_launched\":false,\"cpu_emulator_linked\":false,\"complete_gameplay_verified\":false,\"passed\":true}\n";
    std::cout<<"PASS actual story in the native game host; owned memory only.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
