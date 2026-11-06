#define SDL_MAIN_HANDLED
#include "sdl_scene.hpp"
#include <SDL.h>
#include <cstring>
#include <fstream>
#include <iostream>

namespace p=wa2::platform;namespace s=wa2::script;namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
std::array<unsigned,4> pixel(SDL_Surface* surface,int x,int y){Uint32 value{};std::memcpy(&value,static_cast<const std::uint8_t*>(surface->pixels)+y*surface->pitch+x*4,4);
    std::array<Uint8,4> rgba;SDL_GetRGBA(value,surface->format,&rgba[0],&rgba[1],&rgba[2],&rgba[3]);return {rgba[0],rgba[1],rgba[2],rgba[3]};}
bool close(unsigned value,unsigned expected){return value+2>=expected && value<=expected+2;}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{};SDL_SetMainReady();CHECK(SDL_Init(0)==0);
    // No video subsystem, window, display surface, OS event or GL context.
    // The software renderer owns only this small allocated RGBA buffer.
    std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)> surface(SDL_CreateRGBSurfaceWithFormat(0,64,36,32,SDL_PIXELFORMAT_RGBA32),SDL_FreeSurface);CHECK(surface);
    {
        auto* renderer=SDL_CreateSoftwareRenderer(surface.get());if(!renderer)throw std::runtime_error(std::string("Owned-memory SDL renderer: ")+SDL_GetError());
        auto canvas=p::SdlCanvas::adopt(renderer);canvas->logical_size(1280,720);p::SdlScene scene(*canvas);
        auto red=std::make_shared<i::Raster>(i::Raster{1,1,{255,0,0,255}}),blue=std::make_shared<i::Raster>(i::Raster{1,1,{0,0,255,255}}),green=std::make_shared<i::Raster>(i::Raster{1,1,{0,255,0,255}});
        s::BackgroundFrame frame{{s::BackgroundLayer{red,0,0,1280,720,255}},{s::BackgroundLayer{blue,0,0,1280,720,128}}};
        frame.transition.emplace();frame.transition->current.opacity=128;
        i::Sprite item{green,320,180,640,360,0,0,1,1};
        const auto render=[&](bool sprite=true){scene.background(frame,sprite?std::span(&item,1):std::span<i::Sprite>{});canvas->present();return pixel(surface.get(),32,18);};
        auto colour=render();CHECK(close(colour[0],0) && close(colour[1],255) && close(colour[2],0));++cases;
        colour=pixel(surface.get(),2,2);CHECK(close(colour[0],127) && colour[1]==0 && close(colour[2],128));++cases;
        item.world=i::SpriteWorld::previous;colour=render();CHECK(colour[0]==0 && close(colour[1],127) && close(colour[2],128));++cases;
        item.world=i::SpriteWorld::current;colour=render();CHECK(close(colour[0],127) && close(colour[1],128) && colour[2]==0);++cases;
        colour=render(false);CHECK(close(colour[0],127) && colour[1]==0 && close(colour[2],128));++cases;
        const auto uploads=canvas->statistics().uploads;render(false);CHECK(canvas->statistics().uploads==uploads);++cases;
        auto mask=std::make_shared<i::Raster>(i::Raster{1,1,{0,0,0,128}});frame.mask=s::BackgroundFrame::Mask{mask,0,1};item.world=i::SpriteWorld::both;
        colour=render();CHECK(colour[0]==0 && close(colour[1],255) && colour[2]==0);++cases;
        item.world=i::SpriteWorld::previous;colour=render();CHECK(colour[0]==0 && close(colour[1],127) && close(colour[2],128));++cases;
        frame.mask.reset();frame.transition->previous.transform.x=-640;frame.transition->current.transform.x=640;frame.transition->current.opacity=255;item.world=i::SpriteWorld::both;render();
        CHECK(pixel(surface.get(),8,18)[1]==255 && pixel(surface.get(),56,18)[1]==255);++cases;
        CHECK(pixel(surface.get(),24,18)[0]==255 && pixel(surface.get(),40,18)[2]==255);++cases;
        frame.transition->previous.transform={};frame.transition->current.transform={};frame.transition->current.transform.scale=.5f;colour=render();
        CHECK(colour[1]==255 && pixel(surface.get(),20,18)[2]==255 && pixel(surface.get(),3,3)[0]==255);++cases;
        frame.transition->current.transform.scale=1.75f;render(false);CHECK(pixel(surface.get(),3,3)[2]==255);++cases;
        frame.transition->current.transform={};frame.transition->current.opacity=128;render(false);CHECK(close(pixel(surface.get(),32,18)[0],127) && close(pixel(surface.get(),32,18)[2],128));++cases;
        frame.transform.x=160;colour=render();CHECK(colour[1]==255 && pixel(surface.get(),2,2)[0]==0 && pixel(surface.get(),2,2)[2]==0 && close(pixel(surface.get(),12,2)[2],128));++cases;
        frame.mask=s::BackgroundFrame::Mask{mask,0,1};colour=render();CHECK(close(colour[1],255) && close(pixel(surface.get(),12,2)[0],127) && close(pixel(surface.get(),12,2)[2],128));++cases;
        frame.mask.reset();frame.transform={0,0,1.25f,0};colour=render();CHECK(colour[1]==255 && close(pixel(surface.get(),2,2)[2],128));frame.transform={};++cases;
        frame.mask.reset();frame.transition.reset();frame.previous.reset();frame.current->opacity=255;colour=render(false);CHECK(colour[0]==0 && colour[1]==0 && colour[2]==255);++cases;
        const auto steady_draws=canvas->statistics().draws;render(false);CHECK(canvas->statistics().draws-steady_draws==1);++cases;
        item.image=green;item.blend=i::SpriteBlend::multiply;item.alpha=0;item.world=i::SpriteWorld::current;colour=render();CHECK(colour[0]==0 && colour[1]==0 && colour[2]==0 && pixel(surface.get(),2,2)[2]==255);++cases;
        item.blend=i::SpriteBlend::alpha;colour=render();CHECK(colour[2]==255);++cases;
        item.alpha=255;item.red=item.green=item.blue=128;item.blend=i::SpriteBlend::multiply;frame.current->image=green;colour=render();CHECK(colour[0]==0 && close(colour[1],128) && colour[2]==0);++cases;
        frame.current.reset();red.reset();blue.reset();green.reset();mask.reset();item={};render(false);CHECK(scene.texture_count()==0);++cases;
        scene.reset_textures();
        CHECK((SDL_WasInit(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS)&(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS))==0);++cases;
    }
    surface.reset();SDL_Quit();std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-scene-composition.v1\",\"cases\":"<<cases<<",\"synthetic_only\":true,\"owned_memory_only\":true,\"windows_created\":0,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";out.close();CHECK(out);
    std::cout<<"PASS native world composition in an owned memory buffer.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
