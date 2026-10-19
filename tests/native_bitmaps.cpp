#define SDL_MAIN_HANDLED
#include "scene_bitmaps.hpp"
#include "scene_colour.hpp"
#include "resource_catalog.hpp"
#include "sdl_scene.hpp"
#include "file_io.hpp"
#include <SDL.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace s=wa2::script;namespace r=wa2::resource;namespace p=wa2::platform;namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
unsigned state_cases{},catalog_cases{},render_cases{};
template<class F>void rejects(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("invalid native bitmap accepted");}
std::array<std::uint8_t,4> pixel(SDL_Surface* surface,int x,int y){Uint32 value{};std::memcpy(&value,static_cast<const std::uint8_t*>(surface->pixels)+y*surface->pitch+x*4,4);
    std::array<std::uint8_t,4> out{};SDL_GetRGBA(value,surface->format,&out[0],&out[1],&out[2],&out[3]);return out;}
int main(int argc,char** argv){try{
    CHECK(argc==3);const auto root=std::filesystem::path(argv[1]);unsigned opens=0;
    r::Catalog resources([&](auto name)->std::shared_ptr<r::Source>{++opens;const auto path=root/std::string(name);return std::filesystem::exists(path)?p::open_resource_file(path):nullptr;});
    auto original=resources.stream("grp","sys_00000.tga");CHECK(original && resources.open_banks()>0);++catalog_cases;
    const auto before=opens;resources.stream("GRP","sys_00000.tga");CHECK(opens==before);++catalog_cases;
    rejects([&]{resources.stream("../grp","sys_00000.tga");});rejects([&]{resources.movie("../x.pak");});catalog_cases+=2;
    rejects([&]{resources.image("grp","../sys_00000.tga");});++catalog_cases;
    s::Library library(resources.required("ck-gal"));for(unsigned n=176;n<=192;++n){const auto& function=library.functions().at(n);std::cout<<n<<' '<<function.name<<' '<<function.parameters.size()<<'\n';}
    s::SceneBitmaps bitmaps([&](auto name,auto bank,auto){return resources.image(bank,name);});
    std::array<s::Value,8> load{s::Value(0),s::Value(std::string("sys_00010.tga")),s::Value(5),s::Value(0),s::Value(std::string()),s::Value(256),s::Value(std::string("grp")),s::Value(1)};
    s::Command command{176,load};CHECK(!bitmaps.dispatch(command)->complete);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(bitmaps.slot(0).request && std::chrono::steady_clock::now()<deadline){bitmaps.update();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    CHECK(bitmaps.dispatch(command)->complete && bitmaps.slot(0).image && bitmaps.slot(0).width>=360);++state_cases;
    std::array<s::Value,7> rectangle{s::Value(0),s::Value(10),s::Value(12),s::Value(0),s::Value(0),s::Value(40),s::Value(40)};
    command={189,rectangle};CHECK(bitmaps.dispatch(command)->complete);CHECK(bitmaps.sprites().size()==1 && bitmaps.sprites()[0].x==10 && bitmaps.sprites()[0].source_width==40);++state_cases;
    std::array<s::Value,4> alpha{s::Value(0),s::Value(1),s::Value(0),s::Value(4)};command={185,alpha};bitmaps.dispatch(command);for(int n=0;n<5;++n)bitmaps.update();CHECK(bitmaps.sprites().empty());++state_cases;
    alpha[2]=s::Value(255);alpha[3]=s::Value(0);command={185,alpha};bitmaps.dispatch(command);CHECK(bitmaps.sprites().size()==1);++state_cases;
    std::array<s::Value,2> flip{s::Value(0),s::Value(3)};command={186,flip};bitmaps.dispatch(command);CHECK(bitmaps.sprites()[0].flip==3);++state_cases;
    std::array<s::Value,4> zoom{s::Value(0),s::Value(0),s::Value(0),s::Value(2.f)};command={191,zoom};bitmaps.dispatch(command);CHECK(bitmaps.sprites()[0].x==20 && bitmaps.sprites()[0].width==80);++state_cases;
    std::array<s::Value,4> rotation{s::Value(0),s::Value(0),s::Value(0),s::Value(1024)};command={192,rotation};bitmaps.dispatch(command);CHECK(bitmaps.sprites()[0].angle==90);++state_cases;
    std::array<s::Value,1> index{s::Value(0)};command={180,index};bitmaps.dispatch(command);CHECK(!bitmaps.slot(0).allocated && bitmaps.sprites().empty());++state_cases;
    s::SceneColour colour;std::array<s::Value,4> fade{s::Value(4),s::Value(0),s::Value(64),s::Value(128)};command={153,fade};CHECK(!colour.dispatch(command)->complete);
    colour.update();CHECK((colour.channels()==std::array<std::int32_t,3>{96,112,128}));for(int n=0;n<4;++n)colour.update();CHECK(colour.dispatch(command)->complete && colour.channels()[0]==0);++state_cases;
    std::array<s::Value,5> flash{s::Value(0),s::Value(0),s::Value(255),s::Value(255),s::Value(255)};command={152,flash};colour.dispatch(command);colour.update();CHECK(colour.busy() && colour.channels()[0]==255);colour.update();CHECK(!colour.busy() && colour.channels()[0]==128);++state_cases;
    CHECK(SDL_Init(0)==0);std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)> surface(SDL_CreateRGBSurfaceWithFormat(0,8,8,32,SDL_PIXELFORMAT_RGBA32),SDL_FreeSurface);CHECK(surface);
    {auto canvas=p::SdlCanvas::adopt(SDL_CreateSoftwareRenderer(surface.get()));p::SdlScene display(*canvas);
        auto image=std::make_shared<i::Raster>(i::Raster{2,1,{255,0,0,255,0,0,255,255}});
        std::array<i::Sprite,1> sprites{i::Sprite{image,0,0,8,8,0,0,2,1}};
        canvas->clear({0,0,0,255});display.sprites(sprites);canvas->present();CHECK(pixel(surface.get(),0,4)[0]>200 && pixel(surface.get(),7,4)[2]>200);++render_cases;
        sprites[0].flip=1;canvas->clear({0,0,0,255});display.sprites(sprites);canvas->present();CHECK(pixel(surface.get(),0,4)[2]>200 && pixel(surface.get(),7,4)[0]>200);++render_cases;
        auto solid=std::make_shared<i::Raster>(i::Raster{2,2,{160,100,60,255,160,100,60,255,160,100,60,255,160,100,60,255}});s::BackgroundLayer layer{solid,0,0,8,8,255,{64,128,255}};
        const auto saved_colour=layer.colour;layer.colour={128,128,128};display.background({{},layer,{}});canvas->present();const auto untreated=pixel(surface.get(),4,4);
        std::cout<<"Unmodified owned background "<<unsigned(untreated[0])<<','<<unsigned(untreated[1])<<','<<unsigned(untreated[2])<<'\n';
        layer.colour={64,128,128};display.background({{},layer,{}});canvas->present();const auto multiplied=pixel(surface.get(),4,4);
        std::cout<<"Multiplied owned background "<<unsigned(multiplied[0])<<','<<unsigned(multiplied[1])<<','<<unsigned(multiplied[2])<<'\n';
        layer.colour=saved_colour;
        display.background({{},layer,{}});canvas->present();const auto value=pixel(surface.get(),4,4);
        std::cout<<"Owned-memory background colour "<<unsigned(value[0])<<','<<unsigned(value[1])<<','<<unsigned(value[2])<<'\n';
        CHECK(value[0]>=79 && value[0]<=81 && value[1]>=99 && value[1]<=100 && value[2]>=253);++render_cases;
    }
    surface.reset();SDL_Quit();std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.native-bitmaps.v1\",\"state_cases\":"<<state_cases<<",\"catalog_cases\":"<<catalog_cases<<",\"render_cases\":"<<render_cases
        <<",\"maximum_bitmap_slots\":32,\"screen_access\":false,\"windows_created\":0,\"game_launched\":false,\"cpu_emulator_linked\":false,\"all_image_animation_types_ported\":false,\"passed\":true}\n";
    std::cout<<"PASS native bitmap/colour/resource order: "<<state_cases<<'/'<<catalog_cases<<'/'<<render_cases<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
