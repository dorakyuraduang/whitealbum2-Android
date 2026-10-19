#define SDL_MAIN_HANDLED
#include "sdl_scene.hpp"
#include "scene_controls.hpp"
#include "scene_music.hpp"
#include "script_session.hpp"
#include "file_io.hpp"
#include <SDL.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace s=wa2::script;
namespace i=wa2::image;
namespace r=wa2::resource;
namespace p=wa2::platform;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
unsigned image_cases{},render_cases{},scene_cases{},control_cases{};
template<class F>void rejects(F function){bool failed=false;try{function();}catch(const std::exception&){failed=true;}CHECK(failed);}
struct Bytes final:r::Source {
    std::vector<std::uint8_t> bytes;
    std::atomic<unsigned> reads{};
    std::uint64_t size()const override{return bytes.size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> output)override{
        if(at>bytes.size() || output.size()>bytes.size()-at)throw r::Error("test source range");
        ++reads;if(!output.empty())std::memcpy(output.data(),bytes.data()+std::size_t(at),output.size());
    }
};
void put(std::vector<std::uint8_t>& data,std::size_t at,std::uint32_t value){for(unsigned k=0;k<4;++k)data.at(at+k)=std::uint8_t(value>>(k*8));}
std::shared_ptr<Bytes> fixture(){
    auto source=std::make_shared<Bytes>();source->bytes.resize(16+44*2+21+11);
    auto& bytes=source->bytes;std::memcpy(bytes.data(),"KCAP",4);put(bytes,12,2);
    std::memcpy(bytes.data()+20,"B000000.tga",11);put(bytes,52,104);put(bytes,56,21);
    put(bytes,60,1);std::memcpy(bytes.data()+64,"compressed",10);put(bytes,96,125);put(bytes,100,11);
    bytes[104+2]=2;bytes[104+12]=1;bytes[104+14]=1;bytes[104+16]=24;bytes[104+17]=32;
    bytes[104+18]=3;bytes[104+19]=2;bytes[104+20]=1;
    put(bytes,125,11);put(bytes,129,2);bytes[133]=3;bytes[134]='x';bytes[135]='y';return source;
}
std::shared_ptr<const i::Raster> await(const std::shared_ptr<i::ImageRequest>& request){
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(std::chrono::steady_clock::now()<until){if(auto raster=request->get())return raster;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    throw std::runtime_error("native image worker timed out");
}
void image_tests(){
    auto source=fixture();auto archive=r::Archive::open(source);auto view=archive.open_member(0);const auto before=source->reads.load();
    CHECK(view->size()==21 && source->reads==before);std::array<std::uint8_t,3> pixel{};view->read_at(18,pixel);
    CHECK((pixel==std::array<std::uint8_t,3>{3,2,1}));view->read_at(21,{});rejects([&]{view->read_at(20,pixel);});
    rejects([&]{archive.open_member(0,20);});rejects([&]{archive.open_member(3);});++image_cases;
    auto packed=archive.open_member(1);std::array<std::uint8_t,2> unpacked{};packed->read_at(0,unpacked);
    CHECK(unpacked[0]=='x' && unpacked[1]=='y');rejects([&]{packed->read_at(1,unpacked);});++image_cases;
    std::weak_ptr<r::Source> weak=source;
    {auto copy=archive;source.reset();archive={};CHECK(!weak.expired());}
    CHECK(!weak.expired());view.reset();CHECK(weak.expired());++image_cases;
    source=fixture();i::ImageStore images(r::Archive::open(source));auto request=images.request("b000000.TGA");
    CHECK(images.request("B000000.tga")==request);auto raster=await(request);
    CHECK(raster->width==1 && raster->height==1 && raster->rgba==std::vector<std::uint8_t>({1,2,3,255}));++image_cases;
    request.reset();const auto reads=source->reads.load();auto cached=images.request("B000000.tga");
    CHECK(cached->get()==raster && source->reads==reads);++image_cases;
    rejects([&]{images.request("missing.tga");});auto bad=images.request("compressed");rejects([&]{await(bad);});++image_cases;
    std::shared_ptr<i::ImageRequest> after;
    {i::ImageStore temporary(r::Archive::open(fixture()));after=temporary.request("B000000.tga");await(after);}
    CHECK(after->get()->width==1);++image_cases;
}
void control_tests(){
    std::uint32_t now=100;bool skip=false;
    s::SceneControls controls([&]{return now;},[&]{return skip;},[&]{return skip;});
    const std::array<s::Value,3> frames{s::Value(3),s::Value(0),s::Value(0)};
    s::Command wait{194,frames};CHECK(!controls.dispatch(wait)->complete);CHECK(!controls.dispatch(wait)->complete);
    CHECK(controls.dispatch(wait)->complete);++control_cases;
    const std::array<s::Value,3> milliseconds{s::Value(50),s::Value(1),s::Value(1)};
    wait={194,milliseconds};CHECK(!controls.dispatch(wait)->complete);now=149;CHECK(!controls.dispatch(wait)->complete);
    now=150;CHECK(controls.dispatch(wait)->complete);++control_cases;
    skip=true;wait={194,frames};CHECK(controls.dispatch(wait)->complete);++control_cases;
    wait={194,milliseconds};CHECK(!controls.dispatch(wait)->complete);now=200;CHECK(controls.dispatch(wait)->complete);++control_cases;
    skip=false;s::Command start{195,{}};controls.dispatch(start);const std::array<s::Value,1> time{s::Value(30)};
    s::Command timer{196,time};CHECK(!controls.dispatch(timer)->complete);now=229;CHECK(!controls.dispatch(timer)->complete);
    now=230;CHECK(controls.dispatch(timer)->complete);++control_cases;
    // Original zero/negative frame waits still yield once before completing.
    for(auto value:{0,-1}){const std::array<s::Value,3> zero{s::Value(value),s::Value(0),s::Value(0)};
        wait={194,zero};CHECK(!controls.dispatch(wait)->complete);CHECK(controls.dispatch(wait)->complete);++control_cases;}
    i::ImageStore store(r::Archive::open(fixture()));auto loaded=store.request("B000000.tga");auto pixels=await(loaded);
    unsigned loads=0;s::SceneBackground background([&](auto name){++loads;return store.request(name);});
    const std::array<s::Value,9> args{s::Value(1),s::Value(0),s::Value(0),s::Value(4),s::Value(0),s::Value(0),s::Value(0),s::Value(1280.f),s::Value(720.f)};
    s::Command command{146,args};CHECK(!background.dispatch(command,0)->complete);
    for(unsigned n=1;n<=4;++n){background.update();CHECK(background.frame().current->opacity==(n==4?255:n*255/4));
        CHECK(background.dispatch(command,0)->complete==(n==4));}
    CHECK(loads==1);++control_cases;
    auto bad=args;bad[0]=s::Value(11);command={146,bad};rejects([&]{background.dispatch(command,0);});++control_cases;
}
std::array<std::uint8_t,4> pixel(SDL_Surface* surface,unsigned x,unsigned y){
    Uint32 value{};std::memcpy(&value,static_cast<const std::uint8_t*>(surface->pixels)+y*surface->pitch+x*4,4);
    std::array<std::uint8_t,4> result{};SDL_GetRGBA(value,surface->format,&result[0],&result[1],&result[2],&result[3]);return result;
}
void render_tests(p::SdlCanvas& canvas,p::SdlScene& renderer,SDL_Surface* surface){
    auto red=std::make_shared<i::Raster>(i::Raster{1,1,{255,0,0,255}});
    auto blue=std::make_shared<i::Raster>(i::Raster{1,1,{0,0,255,255}});
    s::BackgroundFrame frame{{s::BackgroundLayer{red,0,0,32,18,255}},{s::BackgroundLayer{blue,0,0,32,18,128}}};
    renderer.background(frame);canvas.present();const auto mixed=pixel(surface,10,7);
    CHECK(mixed[0]>=126 && mixed[0]<=127 && mixed[1]==0 && mixed[2]>=127 && mixed[2]<=128 && mixed[3]==255);
    CHECK(renderer.texture_count()==2);++render_cases;
    const auto uploads=canvas.statistics().uploads;renderer.background(frame);CHECK(canvas.statistics().uploads==uploads);++render_cases;
    frame.previous.reset();red.reset();frame.current->opacity=255;renderer.background(frame);canvas.present();
    CHECK((pixel(surface,10,7)==std::array<std::uint8_t,4>{0,0,255,255}) && renderer.texture_count()==1);++render_cases;
    renderer.reset_textures();CHECK(renderer.texture_count()==0);renderer.background(frame);
    CHECK(canvas.statistics().uploads==uploads+1);++render_cases;
    frame={};blue.reset();renderer.background(frame);canvas.present();CHECK(renderer.texture_count()==0);
    CHECK((pixel(surface,10,7)==std::array<std::uint8_t,4>{0,0,0,255}));++render_cases;
}
int main(int argc,char** argv){try{
    CHECK(argc==3);image_tests();control_tests();CHECK(SDL_Init(0)==0);
    std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)> surface(SDL_CreateRGBSurfaceWithFormat(0,32,18,32,SDL_PIXELFORMAT_RGBA32),SDL_FreeSurface);
    CHECK(surface);std::uint32_t next=0;std::string name;unsigned steps=0;
    {
        auto canvas=p::SdlCanvas::adopt(SDL_CreateSoftwareRenderer(surface.get()));p::SdlScene renderer(*canvas);
        render_tests(*canvas,renderer,surface.get());canvas->logical_size(1280,720);
        const auto root=std::filesystem::path(argv[1]);
        i::ImageStore images(r::Archive::open(p::open_resource_file(root/"bak.pak")));
        s::SceneBackground backgrounds([&](std::string_view file){return images.request(file);});
        std::uint32_t milliseconds=100;s::SceneControls controls([&]{return milliseconds;});
        s::SceneMusic music([](auto,bool)->std::unique_ptr<wa2::audio::Voice>{throw s::Error("music playback not requested by this scene prefix");});
        s::Library library(r::Archive::open(p::open_resource_file(root/"ck-gal.pak")));s::Session session(library);session.start("1001");
        const auto host=[&](s::Command& call){
            if(auto value=controls.dispatch(call))return *value;
            if(auto value=music.dispatch(call))return *value;
            if(auto value=backgrounds.dispatch(call,controls.modes().time))return *value;
            next=call.index;name=library.functions().at(next).name;return s::CommandResult{false,true,{}};
        };
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!next && std::chrono::steady_clock::now()<deadline){
            milliseconds=100+steps*1000/60;
            backgrounds.update();session.advance(host);renderer.background(backgrounds.frame());canvas->present();++steps;
            if(backgrounds.busy())std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(next && next!=146);const auto frame=backgrounds.frame();CHECK(frame.current && frame.current->image && frame.current->opacity==255);
        CHECK(frame.current->image->width && frame.current->image->height);CHECK(canvas->statistics().draws>0);++scene_cases;
        std::cout<<"Actual scene prefix: first original background "<<frame.current->image->width<<'x'<<frame.current->image->height
            <<", next command "<<next<<' '<<name<<'\n';
    }
    surface.reset();SDL_Quit();
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.native-background.v1\",\"image_cases\":"<<image_cases
        <<",\"render_cases\":"<<render_cases<<",\"control_cases\":"<<control_cases<<",\"scene_cases\":"<<scene_cases<<",\"scene_update_steps\":"<<steps
        <<",\"next_unimplemented_command\":"<<next<<",\"next_unimplemented_command_name\":\""<<name
        <<"\",\"original_background_decoded_and_submitted\":true,\"all_background_effects_ported\":false,"
        "\"screen_access\":false,\"windows_created\":0,\"game_launched\":false,\"cpu_emulator_linked\":false,"
        "\"complete_gameplay_verified\":false,\"apk_created\":false,\"passed\":true}\n";
    std::cout<<"PASS native background: "<<image_cases<<" image, "<<render_cases<<" memory-render, "<<scene_cases<<" scene checks\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
