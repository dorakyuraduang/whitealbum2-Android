#define SDL_MAIN_HANDLED
#include "sdl_canvas.hpp"
#include "sdl_scene.hpp"
#include "scene_background.hpp"
#include "file_io.hpp"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace i=wa2::image;namespace s=wa2::script;namespace r=wa2::resource;namespace p=wa2::platform;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
unsigned bmp_cases{},state_cases{},render_cases{},asset_cases{};
template<class F>void rejects(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("invalid mask accepted");}
void put(std::vector<std::uint8_t>& data,std::size_t at,std::uint32_t value){for(unsigned n=0;n<4;++n)data.at(at+n)=std::uint8_t(value>>(n*8));}
std::vector<std::uint8_t> bmp(unsigned width=2,unsigned height=2,unsigned bits=32){
    const unsigned palette=bits==8?8:0,offset=54+palette,stride=(width*bits+31)/32*4;
    std::vector<std::uint8_t> bytes(offset+stride*height);
    bytes[0]='B';bytes[1]='M';put(bytes,2,std::uint32_t(bytes.size()));put(bytes,10,offset);put(bytes,14,40);
    put(bytes,18,width);put(bytes,22,height);bytes[26]=1;bytes[28]=std::uint8_t(bits);
    if(bits==8){put(bytes,46,2);bytes[54]=17;bytes[58]=201;bytes[offset]=1;bytes[offset+1]=0;}
    else if(bits==32){constexpr std::uint8_t alpha[]{128,255,0,64};
        for(unsigned n=0;n<width*height;++n){bytes[offset+n*4]=7;bytes[offset+n*4+1]=11;bytes[offset+n*4+2]=13;
            bytes[offset+n*4+3]=alpha[n%4];}
    }else{bytes[offset]=3;bytes[offset+1]=2;bytes[offset+2]=1;}
    return bytes;
}
void bitmap(){
    const auto bytes=bmp();auto image=i::decode_game_bmp(bytes);
    CHECK(image.width==2 && image.height==2 && image.rgba[0]==13 && image.rgba[1]==11 && image.rgba[2]==7);
    CHECK(image.rgba[3]==0 && image.rgba[7]==64 && image.rgba[11]==128 && image.rgba[15]==255);++bmp_cases;
    auto top=bytes;put(top,22,std::uint32_t(-2));image=i::decode_game_bmp(top);
    CHECK(image.rgba[3]==128 && image.rgba[11]==0);++bmp_cases;
    auto indexed=bmp(2,1,8);image=i::decode_game_bmp(indexed);CHECK(image.rgba[3]==201 && image.rgba[7]==17);++bmp_cases;
    image=i::decode_game_bmp(bmp(1,1,24));CHECK(image.rgba==std::vector<std::uint8_t>({1,2,3,255}));++bmp_cases;
    for(const auto [at,value]:std::array<std::pair<unsigned,std::uint32_t>,7>{{{0,0},{14,12},{18,0},{18,0xffffffff},{22,0x80000000},{30,1},{10,20}}}){
        auto bad=bytes;put(bad,at,value);rejects([&]{i::decode_game_bmp(bad);});++bmp_cases;
    }
    for(unsigned bits:{1,4,16}){auto bad=bytes;bad[28]=std::uint8_t(bits);rejects([&]{i::decode_game_bmp(bad);});++bmp_cases;}
    auto bad=bytes;bad.pop_back();rejects([&]{i::decode_game_bmp(bad);});++bmp_cases;
    indexed.back()=0;indexed[62]=2;rejects([&]{i::decode_game_bmp(indexed);});++bmp_cases;
    put(indexed,46,257);rejects([&]{i::decode_game_bmp(indexed);});++bmp_cases;
}
struct Bytes:r::Source {
    std::vector<std::uint8_t> data;
    std::uint64_t size()const override{return data.size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> output)override{
        if(at>data.size() || output.size()>data.size()-at)throw r::Error("mask fixture bounds");
        if(!output.empty())std::memcpy(output.data(),data.data()+std::size_t(at),output.size());
    }
};
r::Archive fixture(){
    auto source=std::make_shared<Bytes>();auto mask=bmp();std::vector<std::uint8_t> background(21);
    background[2]=2;background[12]=background[14]=1;background[16]=24;background[17]=32;background[18]=255;
    source->data.resize(104+background.size()+mask.size());auto& data=source->data;std::memcpy(data.data(),"KCAP",4);put(data,12,2);
    std::memcpy(data.data()+20,"B000000.tga",11);put(data,52,104);put(data,56,std::uint32_t(background.size()));
    std::memcpy(data.data()+64,"f0000.bmp",10);put(data,96,104+std::uint32_t(background.size()));put(data,100,std::uint32_t(mask.size()));
    std::copy(background.begin(),background.end(),data.begin()+104);std::copy(mask.begin(),mask.end(),data.begin()+104+background.size());
    return r::Archive::open(source);
}
std::shared_ptr<const i::Raster> await(const std::shared_ptr<i::ImageRequest>& request){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(std::chrono::steady_clock::now()<deadline){if(auto result=request->get())return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    throw std::runtime_error("mask worker timed out");
}
void states(){
    i::ImageStore store(fixture());auto bg=store.request("B000000.tga"),mask=store.request("f0000.bmp");auto bg_pixels=await(bg),mask_pixels=await(mask);
    CHECK(mask_pixels->rgba[3]==0 && mask_pixels->rgba[15]==255);++state_cases;
    unsigned requests=0;
    s::SceneBackground backgrounds([&](auto name){return store.request(name);},{},[&](auto name){++requests;return store.request(name);});
    std::array<s::Value,9> args{s::Value(128),s::Value(0),s::Value(0),s::Value(4),s::Value(0),s::Value(0),s::Value(0),s::Value(1280.f),s::Value(720.f)};
    for(int mode=0;mode<4;++mode){
        args[4]=s::Value(mode);s::Command command{146,args};CHECK(!backgrounds.dispatch(command,0)->complete);
        for(int frame=1;frame<4;++frame){backgrounds.update();auto state=backgrounds.frame();CHECK(state.mask && state.current->opacity==255);
            const float alpha=float(frame*255/4)*(1.f/256.f);constexpr float ramp[]{2,1.5f,1.25f,1.125f},gain[]{1,2,4,32};
            CHECK(state.mask->bias==alpha*ramp[mode]-1 && state.mask->gain==gain[mode]);CHECK(!backgrounds.dispatch(command,0)->complete);}
        backgrounds.update();CHECK(backgrounds.dispatch(command,0)->complete && !backgrounds.frame().mask);++state_cases;
    }
    CHECK(requests==4);++state_cases;
    args[0]=s::Value(128|0x1900);args[4]=s::Value(0);s::Command command{146,args};backgrounds.dispatch(command,0);backgrounds.update();
    const auto state=backgrounds.frame();CHECK(state.mask->flip_x && state.mask->flip_y && state.mask->invert);++state_cases;
    args[0]=s::Value(128);args[3]=s::Value(0);command={146,args};backgrounds.dispatch(command,0);backgrounds.update();
    CHECK(backgrounds.dispatch(command,0)->complete && !backgrounds.frame().mask && requests==5);++state_cases;
    args[3]=s::Value(120);command={146,args};backgrounds.dispatch(command,0);backgrounds.update([](auto){return 1;});
    CHECK(backgrounds.dispatch(command,0)->complete && !backgrounds.frame().mask);++state_cases;
    args[3]=s::Value(4);args[4]=s::Value(4);command={146,args};rejects([&]{backgrounds.dispatch(command,0);});++state_cases;
}
std::array<std::uint8_t,4> pixel(SDL_Surface* surface,int x,int y){
    Uint32 value{};std::memcpy(&value,static_cast<std::uint8_t*>(surface->pixels)+y*surface->pitch+x*4,4);
    std::array<std::uint8_t,4> out{};SDL_GetRGBA(value,surface->format,&out[0],&out[1],&out[2],&out[3]);return out;
}
void rendering(){
    CHECK(SDL_Init(0)==0);
    std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)> surface(SDL_CreateRGBSurfaceWithFormat(0,2,2,32,SDL_PIXELFORMAT_RGBA32),SDL_FreeSurface);
    CHECK(surface);
    {auto canvas=p::SdlCanvas::adopt(SDL_CreateSoftwareRenderer(surface.get()));canvas->logical_size(2,2);
        auto target=canvas->target(2,2);canvas->render_to(target,[&]{canvas->clear({0,0,255,255});});
        auto mask=i::decode_game_bmp(bmp());auto texture=canvas->upload(mask,p::CanvasFilter::linear);
        const auto draw=[&](float bias,float gain,bool x=false,bool y=false,bool invert=false){canvas->clear({255,0,0,255});
            canvas->masked(target,texture,mask,bias,gain,x,y,invert);canvas->present();};
        draw(0,1);CHECK(pixel(surface.get(),0,0)[0]==255 && pixel(surface.get(),0,0)[2]==0);
        CHECK(pixel(surface.get(),1,1)[0]==0 && pixel(surface.get(),1,1)[2]==255);++render_cases;
        const auto partial=pixel(surface.get(),1,0);CHECK(partial[0]>=190 && partial[0]<=192 && partial[2]>=63 && partial[2]<=65);++render_cases;
        draw(-1,1);CHECK(pixel(surface.get(),1,1)[0]==255 && pixel(surface.get(),1,1)[2]==0);++render_cases;
        draw(1,1);CHECK(pixel(surface.get(),0,0)[0]==0 && pixel(surface.get(),0,0)[2]==255);++render_cases;
        draw(0,1,true);CHECK(pixel(surface.get(),0,1)[2]==255 && pixel(surface.get(),1,0)[2]==0);++render_cases;
        draw(0,1,false,true);CHECK(pixel(surface.get(),1,0)[2]==255 && pixel(surface.get(),0,1)[2]==0);++render_cases;
        draw(0,1,false,false,true);CHECK(pixel(surface.get(),0,0)[2]==255 && pixel(surface.get(),1,1)[2]==0);++render_cases;
        draw(-.5f,4);CHECK(pixel(surface.get(),1,0)[2]==0 && pixel(surface.get(),1,1)[2]==255);++render_cases;
        rejects([&]{canvas->render_to(texture,[]{});});++render_cases;
        rejects([&]{canvas->render_to(target,[&]{canvas->masked(target,texture,mask,0,1,false,false,false);});});++render_cases;
        auto wrong=mask;wrong.width=1;rejects([&]{canvas->masked(target,texture,wrong,0,1,false,false,false);});++render_cases;
        canvas->logical_size(1280,720);p::SdlScene scene(*canvas);
        auto blue=std::make_shared<i::Raster>(i::Raster{1,1,{0,0,255,255}});
        auto red=std::make_shared<i::Raster>(i::Raster{1,1,{255,0,0,255}});
        auto mask_image=std::make_shared<i::Raster>(mask);
        s::BackgroundFrame frame{{s::BackgroundLayer{red,0,0,1280,720,255}},
            {s::BackgroundLayer{blue,0,0,1280,720,255}},s::BackgroundFrame::Mask{mask_image,-.5f,1}};
        scene.background(frame);canvas->present();const auto uploads=canvas->statistics().uploads;
        CHECK(scene.texture_count()==3);++render_cases;
        frame.mask->bias=-.25f;scene.background(frame);canvas->present();
        CHECK(canvas->statistics().uploads==uploads && scene.texture_count()==3);++render_cases;
        frame.mask.reset();frame.previous.reset();scene.background(frame);canvas->present();
        CHECK(pixel(surface.get(),0,0)[2]==255);++render_cases;
    }
    surface.reset();SDL_Quit();
}
void asset(const std::filesystem::path& directory){
    auto archive=r::Archive::open(p::open_resource_file(directory/"IC/grp.pak"));const auto index=archive.find_first("f0000.bmp");CHECK(index!=r::Archive::missing);
    const auto bytes=archive.load(index);const auto mask=i::decode_game_bmp(bytes);CHECK(mask.width==1280 && mask.height==720);++asset_cases;
    std::array<bool,256> levels{};
    for(unsigned y=0;y<720;++y)for(unsigned x=0;x<1280;++x){const auto alpha=bytes[54+(std::size_t(719-y)*1280+x)*4+3];
        CHECK(mask.rgba[(std::size_t(y)*1280+x)*4+3]==alpha);levels[alpha]=true;}
    CHECK(std::count(levels.begin(),levels.end(),true)>100);++asset_cases;
    for(const auto name:{"pf00.psh","pf01.psh","pf02.psh","pf03.psh"}){
        const auto member=archive.find_first(name);CHECK(member!=r::Archive::missing);const auto code=archive.load(member);
        const std::string_view text(reinterpret_cast<const char*>(code.data()),code.size());CHECK(text.find("ps.1.1")!=std::string_view::npos && text.find("t1.a, c0.a")!=std::string_view::npos);++asset_cases;
    }
}
int main(int argc,char** argv){try{
    CHECK(argc==3);bitmap();states();rendering();asset(argv[1]);
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.native-masks.v1\",\"bitmap_cases\":"<<bmp_cases<<",\"state_cases\":"<<state_cases
        <<",\"render_cases\":"<<render_cases<<",\"asset_cases\":"<<asset_cases<<",\"gpu_shader_device_verified\":false,\"software_reference_only\":true,"
        "\"screen_access\":false,\"windows_created\":0,\"game_launched\":false,\"cpu_emulator_linked\":false,\"passed\":true}\n";
    std::cout<<"PASS masks: "<<bmp_cases<<" BMP, "<<state_cases<<" state, "<<render_cases<<" owned-memory render, "<<asset_cases<<" original asset checks\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
