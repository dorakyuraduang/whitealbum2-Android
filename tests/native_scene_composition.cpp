#define SDL_MAIN_HANDLED
#include "sdl_scene.hpp"
#include "sdl_presentation.hpp"
#include "sprite_atlas.hpp"
#include "font_glyphs.hpp"
#include "scene_dialogue.hpp"
#include "confirmation_view.hpp"
#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>

namespace p=wa2::platform;namespace s=wa2::script;namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
std::array<unsigned,4> pixel(SDL_Surface* surface,int x,int y){Uint32 value{};std::memcpy(&value,static_cast<const std::uint8_t*>(surface->pixels)+y*surface->pitch+x*4,4);
    std::array<Uint8,4> rgba;SDL_GetRGBA(value,surface->format,&rgba[0],&rgba[1],&rgba[2],&rgba[3]);return {rgba[0],rgba[1],rgba[2],rgba[3]};}
bool close(unsigned value,unsigned expected){return value+2>=expected && value<=expected+2;}
namespace text_alpha {
using Bytes=std::vector<std::uint8_t>;namespace r=wa2::resource;namespace t=wa2::text;namespace g=wa2::game;
void put(Bytes& bytes,std::size_t at,unsigned value){for(unsigned n=0;n<4;++n)bytes.at(at+n)=std::uint8_t(value>>(8*n));}
struct Source:r::Source {
    Bytes bytes;std::uint64_t size()const override{return bytes.size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{CHECK(at<=bytes.size() && out.size()<=bytes.size()-at);std::copy_n(bytes.data()+at,out.size(),out.data());}
};
Bytes transparent(unsigned width,unsigned height){Bytes bytes(18+width*height*4);bytes[2]=2;bytes[12]=std::uint8_t(width);bytes[13]=std::uint8_t(width>>8);bytes[14]=std::uint8_t(height);bytes[15]=std::uint8_t(height>>8);bytes[16]=32;bytes[17]=40;return bytes;}
Bytes font(unsigned index){
    const auto pitch=index<2?40u:24u,width=pitch*80,height=pitch*(t::glyph_count(true)/80+1);
    Bytes bytes(18+16+width*height);bytes[1]=bytes[2]=1;bytes[5]=4;bytes[7]=32;bytes[12]=std::uint8_t(width);bytes[13]=std::uint8_t(width>>8);bytes[14]=std::uint8_t(height);bytes[15]=std::uint8_t(height>>8);bytes[16]=8;bytes[17]=32;
    // Independent colour/alpha, including descriptor=0 in the low bits:
    // transparent white padding, opaque black ink, coloured antialiasing.
    const std::array<std::array<std::uint8_t,4>,4> palette{{{255,255,255,0},{0,0,0,255},{199,73,17,96},{255,255,255,0}}};
    for(unsigned n=0;n<4;++n)std::copy(palette[n].begin(),palette[n].end(),bytes.begin()+18+n*4);
    for(auto code:{std::uint16_t('A'),std::uint16_t('B'),std::uint16_t(0x93c7)}){
        const auto box=t::glyph_box(code,true,pitch,0,pitch);const auto shadow=index%2!=0;
        const unsigned core=shadow?pitch/5:pitch*3/10,edge=core-2;
        for(unsigned y=edge;y<pitch-edge;++y)for(unsigned x=edge;x<pitch-edge;++x)
            bytes[34+(box.y+y)*width+box.x+x]=x>=core && x<pitch-core && y>=core && y<pitch-core?1:2;
    }
    return bytes;
}
unsigned verify(){
    unsigned cases{},font_reads{};
    const std::array<std::string_view,4> names{"\x96\x7b\x91\xcc" "80.tga","\x91\xdc\x89\x65" "80.tga","14pt\x96\x7b\x91\xcc.tga","14pt\x91\xdc.tga"};
    t::FontBook fonts([&](std::string_view name,std::size_t limit){const auto found=std::find(names.begin(),names.end(),name);CHECK(found!=names.end());auto result=font(unsigned(found-names.begin()));CHECK(result.size()<=limit);++font_reads;return result;});
    auto source=std::make_shared<Source>();auto& bytes=source->bytes;
    const std::array<std::string_view,6> images{"sys_00000.tga","sys_00001.tga","sys_00010.tga","sys_00011.tga","sys_00100.tga","sys_00101.tga"};
    bytes.resize(16+images.size()*44);std::memcpy(bytes.data(),"KCAP",4);put(bytes,12,unsigned(images.size()));
    for(unsigned n=0;n<images.size();++n){auto image=transparent(n==5?320:784,n==5?120:216);std::memcpy(bytes.data()+20+n*44,images[n].data(),images[n].size());put(bytes,52+n*44,unsigned(bytes.size()));put(bytes,56+n*44,unsigned(image.size()));bytes.insert(bytes.end(),image.begin(),image.end());}
    i::ImageStore store(r::Archive::open(source));
    const s::SceneDialogue::Loader loader=[&](auto name){auto request=store.request(name);CHECK(request->wait_for(std::chrono::seconds(5)));return request;};
    t::TextStyle style;style.line_gap=0;
    auto prepared=fonts.prepare(t::layout("A",style));std::vector<i::Sprite> glyphs;prepared.append(glyphs,0,1000,20,20);CHECK(glyphs.size()==2 && font_reads==2);++cases;
    const auto& face=glyphs.back();const auto& atlas=*face.image;
    const auto coverage=[&](unsigned x,unsigned y){return atlas.rgba[(y*atlas.width+x)*4+3];};
    CHECK(coverage(0,0)==0 && coverage(12,12)==255 && coverage(10,15)==96);++cases;
    CHECK(coverage(40,0)==0 && coverage(48,15)==255 && coverage(46,15)==96);++cases;
    CHECK(face.red==255 && glyphs.front().red==0 && face.alpha==255);++cases;
    // Include a two-byte game glyph and ruby so all four font banks pass
    // through the same native mask preparation; no original pixels are used.
    auto mixed=fonts.prepare(t::layout("\x93\xc7<RA|B>",style));std::vector<i::Sprite> mixed_glyphs;mixed.append(mixed_glyphs,0,1000,0,0);CHECK(mixed_glyphs.size()==6 && font_reads==4);++cases;
    std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)> surface(SDL_CreateRGBSurfaceWithFormat(0,1280,720,32,SDL_PIXELFORMAT_RGBA32),SDL_FreeSurface);CHECK(surface);
    auto canvas=p::SdlCanvas::adopt(SDL_CreateSoftwareRenderer(surface.get()));CHECK(canvas);canvas->logical_size(1280,720);p::SdlScene scene(*canvas);
    const auto draw=[&](std::span<const i::Sprite> sprites){canvas->clear({50,100,150,255});scene.sprites(sprites);canvas->present();};
    const auto verify_glyph=[&](unsigned x,unsigned y,bool modal){
        const auto padding=pixel(surface.get(),int(x+2),int(y+2)),ink=pixel(surface.get(),int(x+20),int(y+20)),outline=pixel(surface.get(),int(x+8),int(y+15)),edge=pixel(surface.get(),int(x+10),int(y+15));
        CHECK(close(padding[0],modal?35:50) && close(padding[1],modal?70:100) && close(padding[2],modal?105:150));++cases;
        CHECK(ink[0]==255 && ink[1]==255 && ink[2]==255);++cases;
        CHECK(outline[0]==0 && outline[1]==0 && outline[2]==0);++cases;
        CHECK(close(edge[0],96) && close(edge[1],96) && close(edge[2],96));++cases;
    };
    draw(glyphs);verify_glyph(20,20,false);const auto uploads=canvas->statistics().uploads;draw(glyphs);verify_glyph(20,20,false);CHECK(canvas->statistics().uploads==uploads);++cases;
    s::SceneDialogue dialogue(fonts,loader);s::DialogueContent content;content.open=true;content.raw="A";content.budget=32767;dialogue.restore_content(content);dialogue.update();CHECK(dialogue.ready());draw(dialogue.sprites());verify_glyph(274,550,false);++cases;
    g::ConfirmationView confirmation(loader,[&](auto raw){return fonts.prepare(t::layout(raw,style));});confirmation.prepare("A",0,0,0);confirmation.update();CHECK(confirmation.phase()==g::ConfirmationPhase::active);draw(confirmation.sprites());verify_glyph(516,304,true);++cases;
    confirmation.prepare("<F20A>",0,0,0);confirmation.update();draw(confirmation.sprites());CHECK(pixel(surface.get(),530,318)[0]==255 && close(pixel(surface.get(),517,305)[0],35));++cases;
    CHECK(font_reads==4);++cases;
    CHECK((SDL_WasInit(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS)&(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS))==0);++cases;
    return cases;
}
}
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
        {
            const std::array<std::shared_ptr<const i::Raster>,3> images{red,green,blue};std::array<i::Sprite,6> quads;
            for(unsigned n=0;n<quads.size();++n)quads[n]={images[n%3],float(n*200),200,200,200,0,0,1,1};
            canvas->clear({0,0,0,255});const auto before=canvas->statistics().draws;scene.sprites(quads);canvas->present();CHECK(canvas->statistics().draws-before==6);++cases;
            std::array<std::array<unsigned,4>,6> expected;for(unsigned n=0;n<6;++n)expected[n]=pixel(surface.get(),5+10*n,15);
            const auto atlas=i::make_sprite_atlas(images,16);CHECK(atlas);for(unsigned n=0;n<6;++n){quads[n].image=atlas->image;quads[n].source_x=float(atlas->regions[n%3].x);quads[n].source_y=float(atlas->regions[n%3].y);}
            canvas->clear({0,0,0,255});const auto batched=canvas->statistics().draws;scene.sprites(quads);canvas->present();CHECK(canvas->statistics().draws-batched==1);++cases;
            for(unsigned n=0;n<6;++n)CHECK(pixel(surface.get(),5+10*n,15)==expected[n]);++cases;
        }
        {
            p::SdlPresentation presentation(*canvas);wa2::game::RenderFrame output;
            s::BackgroundFrame world;world.current=s::BackgroundLayer{blue,0,0,1280,720,255};
            std::array<i::Sprite,3> overlay;const auto quad=[&](auto image,int layer,int order,i::SpriteSpace space=i::SpriteSpace::screen){i::Sprite value{image,0,0,1280,720,0,0,1,1};value.layer=layer;value.order=order;value.space=space;return value;};
            const auto draw=[&]{presentation.draw(output);canvas->present();return pixel(surface.get(),32,18);};
            overlay[0]=quad(red,0x105b,0x444,i::SpriteSpace::dialogue);overlay[1]=quad(green,0x1062,0x53a);
            const auto pair=std::span<const i::Sprite>(overlay).first(2);const std::array groups{pair};
            output.compose(world,{},groups);colour=draw();CHECK(colour[1]==255 && colour[0]==0);++cases;
            overlay[1].layer=0x104c;output.compose(world,{},groups);colour=draw();CHECK(colour[0]==255 && colour[1]==0);++cases;
            overlay[1].layer=overlay[0].layer;overlay[1].order=0x445;output.compose(world,{},groups);colour=draw();CHECK(colour[1]==255);++cases;
            overlay[0]=quad(red,0x1057,0x11);overlay[1]=quad(green,0x1057,0x11);overlay[0].alpha=overlay[1].alpha=128;
            output.compose(world,{},groups);colour=draw();CHECK(close(colour[0],63) && close(colour[1],128) && close(colour[2],63));++cases;
            std::swap(overlay[0],overlay[1]);output.compose(world,{},groups);colour=draw();CHECK(close(colour[0],128) && close(colour[1],63) && close(colour[2],63));++cases;
            for(unsigned n=0;n<3;++n){overlay[n]=quad(n==1?green:red,0x104d+int(n),0x40d+int(n),n==1?i::SpriteSpace::screen:i::SpriteSpace::dialogue);overlay[n].width=320;overlay[n].height=180;overlay[n].x=n==2?320:0;}
            const std::array all{std::span<const i::Sprite>(overlay)};output.compose(world,{},all,{640,0,1,0});draw();
            CHECK(pixel(surface.get(),4,4)[1]==255 && pixel(surface.get(),20,4)[2]==255 && pixel(surface.get(),36,4)[0]==255 && pixel(surface.get(),56,4)[0]==255);++cases;
            // Equal transforms batch through space tags; different transforms
            // retain their layer positions even when they use the same image.
            overlay[1].image=red;output.compose(world,{},all);auto before=canvas->statistics().draws;draw();CHECK(canvas->statistics().draws-before==2);++cases;
            output.compose(world,{},all,{640,0,1,0});before=canvas->statistics().draws;draw();CHECK(canvas->statistics().draws-before==4);++cases;
            output.compose(world,{}, {},{},true);colour=draw();CHECK(colour[0]==0 && colour[1]==0 && colour[2]==0);++cases;
            auto pixels=std::make_shared<std::array<std::uint8_t,12>>();std::fill_n(pixels->data(),8,235);std::fill_n(pixels->data()+8,4,128);
            std::weak_ptr<const void> held=pixels;wa2::movie::VideoFrame video;video.owner=pixels;video.width=4;video.height=2;video.planes={wa2::movie::Plane{pixels->data(),4,4,2},wa2::movie::Plane{pixels->data()+8,2,2,1},wa2::movie::Plane{pixels->data()+10,2,2,1}};
            output.compose({}, {},{}, {},true,video);colour=draw();CHECK(colour[0]>250 && colour[1]>250 && colour[2]>250 && pixel(surface.get(),32,0)[0]==0 && pixel(surface.get(),32,35)[0]==0);++cases;
            auto uploaded=canvas->statistics().uploads;draw();CHECK(canvas->statistics().uploads==uploaded);++cases;
            video.aspect_denominator=2;output.compose({}, {},{}, {},true,video);colour=draw();CHECK(colour[0]>250 && pixel(surface.get(),4,18)[0]==0 && pixel(surface.get(),59,18)[0]==0 && canvas->statistics().uploads==uploaded);++cases;
            video.time_us+=1000;output.compose({}, {},{}, {},true,video);draw();CHECK(canvas->statistics().uploads==uploaded);++cases;
            overlay[0]=quad(green,0x2000,0);overlay[0].x=640;overlay[0].y=600;overlay[0].width=320;overlay[0].height=120;
            const std::array captions{std::span<const i::Sprite>(overlay).first(1)};output.compose({}, {},captions,{},true,video);draw();CHECK(pixel(surface.get(),36,32)[1]==255 && pixel(surface.get(),36,32)[0]==0 && pixel(surface.get(),32,18)[0]>250);++cases;
            presentation.reset_textures();uploaded=canvas->statistics().uploads;draw();CHECK(canvas->statistics().uploads==uploaded+2 && pixel(surface.get(),36,32)[1]==255);++cases;
            output.compose(world,{},{});colour=draw();CHECK(colour[2]==255 && colour[0]==0 && colour[1]==0);++cases;
            video={};pixels.reset();CHECK(held.expired());++cases;
            output.clear();colour=draw();CHECK(colour[0]==0 && colour[1]==0 && colour[2]==0);++cases;
            presentation.reset_textures();CHECK(!presentation.texture_count());++cases;
        }
        frame.current.reset();red.reset();blue.reset();green.reset();mask.reset();item={};render(false);CHECK(scene.texture_count()==0);++cases;
        scene.reset_textures();
        CHECK((SDL_WasInit(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS)&(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS))==0);++cases;
    }
    surface.reset();const auto text_alpha_cases=text_alpha::verify();cases+=text_alpha_cases;SDL_Quit();std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-scene-composition.v1\",\"cases\":"<<cases<<",\"text_alpha_cases\":"<<text_alpha_cases<<",\"independent_font_colour_alpha\":true,\"story_and_confirmation_text_pixels\":true,\"synthetic_only\":true,\"owned_memory_only\":true,\"windows_created\":0,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";out.close();CHECK(out);
    std::cout<<"PASS native world composition in an owned memory buffer.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
