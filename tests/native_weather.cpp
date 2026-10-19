#define SDL_MAIN_HANDLED
#include "scene_weather.hpp"
#include "sdl_scene.hpp"
#include "file_io.hpp"
#include <SDL.h>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace s=wa2::script;namespace i=wa2::image;namespace p=wa2::platform;namespace r=wa2::resource;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
unsigned state_cases{},render_cases{};
template<class F>void rejects(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("invalid weather accepted");}
std::shared_ptr<const i::Raster> await(const std::shared_ptr<i::ImageRequest>& request){
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(std::chrono::steady_clock::now()<end){if(auto image=request->get())return image;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    throw std::runtime_error("weather image timeout");
}
int main(int argc,char** argv){try{
    CHECK(argc==3);i::ImageStore images(r::Archive::open(p::open_resource_file(std::filesystem::path(argv[1])/"IC/grp.pak")));
    auto held=images.request("weather.tga");auto image=await(held);CHECK(image->width>=32 && image->height>=64);++state_cases;
    wa2::Random random(1);CHECK(random.next()==41 && random.next()==18467 && random.next()==6334);++state_cases;random.seed(1);
    s::SceneWeather weather([&](auto name){return images.request(name);},[&]{return random.next();});
    std::array<s::Value,6> args{s::Value(4),s::Value(10),s::Value(10),s::Value(128),s::Value(2),s::Value(0)};
    s::Command command{173,args};CHECK(!weather.dispatch(command)->complete);weather.update();CHECK(weather.dispatch(command)->complete);
    CHECK(weather.active() && weather.type()==4 && weather.extent()==0);++state_cases;
    for(int n=1;n<8;++n)weather.update();CHECK(weather.extent()==1 && weather.sprites().size()==1);++state_cases;
    const auto& first=weather.particles()[0];
    CHECK(first.kind==1 && first.phase==1 && first.rate==.4f && first.radius==float(134.0+(134+100+10)/400.0));
    CHECK(first.angle==float(107.0/3.141592) && first.rotation==89.2f);++state_cases;
    const auto snapshot=weather.sprites()[0];CHECK(snapshot.alpha==0 && snapshot.source_x==0 && snapshot.source_y==32 && snapshot.source_width==32);
    CHECK(std::isfinite(snapshot.x) && std::isfinite(snapshot.y) && snapshot.width>0);++state_cases;
    for(int n=0;n<3000;++n){weather.update();CHECK(weather.extent()<=128 && weather.sprites().size()<=128);
        for(const auto& item:weather.sprites())CHECK(std::isfinite(item.x) && std::isfinite(item.width));}
    ++state_cases;
    const std::array<s::Value,5> unchanged{s::Value(-1000),s::Value(-1000),s::Value(-1000),s::Value(-1000),s::Value(-1000)};
    command={174,unchanged};CHECK(weather.dispatch(command)->complete && weather.scope()==0 && weather.priority()==2);++state_cases;
    command={175,{}};CHECK(!weather.dispatch(command)->complete);
    bool stopped=false;for(int n=0;n<2000 && !stopped;++n){weather.update();stopped=weather.dispatch(command)->complete;}
    CHECK(stopped && !weather.active() && weather.sprites().empty());++state_cases;
    args[0]=s::Value(260);args[3]=s::Value(10000);command={173,args};weather.dispatch(command);weather.update();
    CHECK(weather.extent()==320);++state_cases;
    for(int n=0;n<1000;++n)weather.update();CHECK(weather.extent()==400 && weather.sprites().size()==400);++state_cases;
    const std::array<s::Value,5> independent{s::Value(-1000),s::Value(-1000),s::Value(-1000),s::Value(-1000),s::Value(2)};
    command={174,independent};weather.dispatch(command);weather.stop(true);CHECK(weather.active() && weather.scope()==0);
    weather.stop(true);CHECK(!weather.active());++state_cases;
    args[0]=s::Value(3);args[3]=s::Value(128);command={173,args};weather.dispatch(command);weather.update();
    random.seed(1);for(int n=0;n<1000;++n){weather.update();CHECK(weather.extent()<=128);for(const auto& item:weather.sprites())CHECK(std::isfinite(item.x) && item.width>0 && item.width<=514);}
    CHECK(weather.type()==3 && weather.extent()>0);++state_cases;
    command={175,{}};weather.dispatch(command);bool snow_stopped=false;
    for(int n=0;n<3000 && !snow_stopped;++n){weather.update();snow_stopped=weather.dispatch(command)->complete;}
    CHECK(snow_stopped);++state_cases;
    args[0]=s::Value(0);command={173,args};rejects([&]{weather.dispatch(command);});++state_cases;
    CHECK(SDL_Init(0)==0);std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)> surface(SDL_CreateRGBSurfaceWithFormat(0,32,18,32,SDL_PIXELFORMAT_RGBA32),SDL_FreeSurface);
    CHECK(surface);
    {auto canvas=p::SdlCanvas::adopt(SDL_CreateSoftwareRenderer(surface.get()));p::SdlScene scene(*canvas);canvas->logical_size(1280,720);
        std::vector<i::Sprite> items(400,i::Sprite{image,0,0,32,32,0,32,32,32,255,255,255,128});
        canvas->clear({0,0,0,255});const auto initial=canvas->statistics();scene.sprites(items);canvas->present();const auto after=canvas->statistics();
        CHECK(after.draws-initial.draws==1 && after.vertices-initial.vertices==2400 && after.uploads-initial.uploads==1);++render_cases;
        scene.sprites(items);CHECK(canvas->statistics().uploads==after.uploads && scene.texture_count()==1);++render_cases;
        items[0].source_y=float(image->height);rejects([&]{scene.sprites(items);});++render_cases;
    }
    surface.reset();SDL_Quit();
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.native-weather.v1\",\"state_cases\":"<<state_cases<<",\"render_cases\":"<<render_cases
        <<",\"maximum_particles\":400,\"draws_for_400_same_texture_particles\":1,\"implemented_weather_types\":[3,4],\"all_weather_types_ported\":false,"
        "\"screen_access\":false,\"windows_created\":0,\"game_launched\":false,\"cpu_emulator_linked\":false,\"android_device_verified\":false,\"passed\":true}\n";
    std::cout<<"PASS weather: "<<state_cases<<" state, "<<render_cases<<" owned-memory render checks\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
