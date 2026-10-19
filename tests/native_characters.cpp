#define SDL_MAIN_HANDLED
#include "scene_characters.hpp"
#include "resource_catalog.hpp"
#include "sdl_scene.hpp"
#include "file_io.hpp"
#include <SDL.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace s=wa2::script;namespace r=wa2::resource;namespace p=wa2::platform;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
int main(int argc,char** argv){try{
    CHECK(argc==3);const auto root=std::filesystem::path(argv[1]);unsigned cases=0;
    r::Catalog resources([&](auto name)->std::shared_ptr<r::Source>{const auto path=root/std::string(name);return std::filesystem::exists(path)?p::open_resource_file(path):nullptr;});
    s::Library library(resources.required("ck-gal"));for(unsigned n=138;n<=157;++n){const auto& f=library.functions().at(n);std::cout<<n<<' '<<f.name<<' '<<f.parameters.size()<<'\n';}
    s::SceneCharacters characters([&](auto name){CHECK(name=="tak001111.tga");return resources.image("char",name);});
    std::array<s::Value,6> args{s::Value(10),s::Value(1111),s::Value(1),s::Value(0),s::Value(256),s::Value(128)};
    s::Command load{155,args};CHECK(!characters.dispatch(load)->complete);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(characters.busy(10) && std::chrono::steady_clock::now()<deadline){characters.update();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    CHECK(characters.dispatch(load)->complete && characters.count()==1 && characters.sprites().size()==1);++cases;
    auto sprite=characters.sprites()[0];CHECK(sprite.x==float((1280-int(sprite.image->width))/2) && sprite.y==float(720-int(sprite.image->height)) && sprite.alpha==255 && sprite.layer==2);++cases;
    std::weak_ptr<const wa2::image::Raster> retained=sprite.image;sprite={};
    std::array<s::Value,8> change{s::Value(10),s::Value(1111),s::Value(2),s::Value(1),s::Value(1),s::Value(4),s::Value(128),s::Value(64)};
    s::Command move{154,change};CHECK(!characters.dispatch(move)->complete);characters.update();CHECK(characters.busy(10));
    for(unsigned n=0;n<4;++n)characters.update();CHECK(characters.dispatch(move)->complete && characters.sprites()[0].x==float((1280-int(characters.sprites()[0].image->width))/2+288));++cases;
    CHECK(characters.sprites()[0].alpha==128 && characters.sprites()[0].red==128 && characters.sprites()[0].layer==3);++cases;
    characters.retire_background();args[2]=s::Value(-13.5f);load={155,args};CHECK(characters.dispatch(load)->complete);characters.update();characters.clear_retired();CHECK(characters.count()==1);++cases;
    CHECK(characters.sprites()[0].x==float((1280-int(characters.sprites()[0].image->width))/2)-13.5f);++cases;
    CHECK(SDL_Init(0)==0);std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)> surface(SDL_CreateRGBSurfaceWithFormat(0,64,36,32,SDL_PIXELFORMAT_RGBA32),SDL_FreeSurface);CHECK(surface);
    {auto canvas=p::SdlCanvas::adopt(SDL_CreateSoftwareRenderer(surface.get()));canvas->logical_size(1280,720);p::SdlScene display(*canvas);
        canvas->clear({0,0,0,255});display.sprites(characters.sprites());canvas->present();}
    surface.reset();SDL_Quit();++cases;
    std::array<s::Value,3> remove{s::Value(10),s::Value(0),s::Value(4)};s::Command end{156,remove};CHECK(!characters.dispatch(end)->complete);characters.update();CHECK(characters.sprites()[0].alpha<255);++cases;
    for(unsigned n=0;n<4;++n)characters.update();CHECK(characters.dispatch(end)->complete && !characters.count() && characters.sprites().empty() && retained.expired());++cases;
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.native-characters.v1\",\"cases\":"<<cases<<",\"maximum_character_slots\":8,\"original_pak_artwork\":true,\"screen_access\":false,\"windows_created\":0,\"cpu_emulator_linked\":false,\"passed\":true}\n";
    std::cout<<"PASS native characters: "<<cases<<" cases.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
