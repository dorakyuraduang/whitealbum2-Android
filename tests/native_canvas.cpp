#define SDL_MAIN_HANDLED
#include "sdl_canvas.hpp"
#include <SDL.h>
#include <fstream>
#include <iostream>
#include <thread>

namespace p=wa2::platform;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
std::array<p::CanvasVertex,6> quad(float x,float y,float width,float height,p::CanvasColour colour={}){
    return {{{x,y,0,0,colour},{x+width,y,1,0,colour},{x+width,y+height,1,1,colour},
             {x,y,0,0,colour},{x+width,y+height,1,1,colour},{x,y+height,0,1,colour}}};
}
std::array<std::uint8_t,4> pixel(SDL_Surface* surface,int x,int y){
    std::array<std::uint8_t,4> colour{};
    SDL_GetRGBA(*(static_cast<std::uint32_t*>(surface->pixels)+y*(surface->pitch/4)+x),surface->format,&colour[0],&colour[1],&colour[2],&colour[3]);return colour;
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("usage: wa2_canvas_tests report.json");
    SDL_SetMainReady();CHECK(SDL_Init(0)==0);
    auto* surface=SDL_CreateRGBSurfaceWithFormat(0,4,4,32,SDL_PIXELFORMAT_RGBA32);CHECK(surface);
    unsigned checks=0,presented=0;
    {
        auto canvas=p::SdlCanvas::adopt(SDL_CreateSoftwareRenderer(surface));canvas->logical_size(4,4);
        wa2::image::Raster source{2,2,{255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255}};
        auto texture=canvas->upload(source,p::CanvasFilter::nearest);
        canvas->clear({0,0,0,255});canvas->triangles(texture,quad(0,0,4,4),p::CanvasBlend::copy);
        canvas->present([&]{++presented;});
        CHECK((pixel(surface,0,0)==std::array<std::uint8_t,4>{255,0,0,255}));
        CHECK((pixel(surface,3,0)==std::array<std::uint8_t,4>{0,255,0,255}));
        CHECK((pixel(surface,0,3)==std::array<std::uint8_t,4>{0,0,255,255}));
        CHECK((pixel(surface,3,3)==std::array<std::uint8_t,4>{255,255,255,255}));++checks;
        canvas->clear({0,0,0,255});canvas->clip(p::CanvasRect{1,1,2,2});
        canvas->triangles({},quad(0,0,4,4,{240,100,50,255}),p::CanvasBlend::copy);canvas->present();
        CHECK(pixel(surface,0,0)[0]==0 && pixel(surface,1,1)[0]==240 && pixel(surface,3,3)[0]==0);++checks;
        canvas->clip({});canvas->clear({0,0,255,255});
        canvas->triangles({},quad(0,0,4,4,{255,0,0,128}),p::CanvasBlend::alpha);canvas->present();
        const auto blended=pixel(surface,1,1);CHECK(blended[0]>=127 && blended[0]<=128 && blended[2]>=126 && blended[2]<=128);++checks;
        const auto statistics=canvas->statistics();CHECK(statistics.presents==3 && statistics.draws==3 && statistics.vertices==18 && statistics.uploads==1 && presented==1);++checks;
        // Releasing a texture on an asset thread queues destruction. Entering
        // the render thread drains that queue without running graphics there.
        std::thread release([owned=std::move(texture)]() mutable{owned.reset();});release.join();canvas->clear({0,0,0,255});++checks;
        bool foreign_rejected=false;std::thread foreign([&]{try{canvas->clear({0,0,0,255});}catch(const std::runtime_error&){foreign_rejected=true;}});foreign.join();CHECK(foreign_rejected);++checks;
        const auto invalid=quad(0,0,4,4);
        bool invalid_rejected=false;try{canvas->triangles({},std::span(invalid).first(2));}catch(const std::runtime_error&){invalid_rejected=true;}CHECK(invalid_rejected);++checks;
        auto survivor=canvas->upload(source);canvas.reset();survivor.reset();++checks;
    }
    SDL_FreeSurface(surface);SDL_Quit();
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-canvas.v1\",\"cases\":"<<checks
        <<",\"surface_bytes\":64,\"software_reference_only\":true,\"windows_created\":0,\"screen_access\":false,\"game_launched\":false"
        <<",\"effect_shaders_ported\":false,\"android_device_verified\":false,\"passed\":true}\n";report.close();CHECK(report);
    std::cout<<"PASS native SDL canvas: "<<checks<<" offscreen software cases; no windows, screen access or game process.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
