#define SDL_MAIN_HANDLED
#include "scene_dialogue.hpp"
#include "sdl_scene.hpp"
#include "file_io.hpp"
#include <SDL.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace s=wa2::script;namespace t=wa2::text;namespace p=wa2::platform;namespace r=wa2::resource;namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
unsigned layout_cases{},font_cases{},state_cases{},render_cases{};
template<class F>void rejects(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("invalid dialogue accepted");}
void layout_checks(){
    auto text=t::layout("AB^C~`\\<");CHECK(text.letters.size()==7 && text.letters[0].x==0 && text.letters[1].x==14 && text.letters[0].y==14);
    CHECK(text.letters[2].code==' ' && text.letters[4].code==',' && text.letters[5].code=='\'' && text.letters[6].code=='<');++layout_cases;
    text=t::layout(std::string("\x90\xcb\x8d\xef\xb1",5));CHECK(text.letters.size()==3 && text.letters[0].code==0x90cb && text.letters[1].x==28 && text.letters[2].code==0xffb1);++layout_cases;
    text=t::layout("A\\nB\\kC");CHECK(text.pages.size()==2 && text.letters[1].x==0 && text.letters[1].y==56 && text.letters[2].page==1);++layout_cases;
    text=t::layout("<C4:red> <B000:255:000:green> <F14:small>");CHECK((text.letters[0].colour==t::Colour{255,0,0}) && (text.letters[4].colour==t::Colour{0,255,0}));++layout_cases;
    text=t::layout("<S0:AB><W12:C>");CHECK(text.letters[1].time==100 && text.letters[2].time==320 && text.pages[0].duration==330);++layout_cases;
    text=t::layout(std::string("<A\x90\xcb>",5));CHECK(text.letters.size()==2 && text.letters[1].small && text.letters[1].code==0x8145);++layout_cases;
    text=t::layout("<RAB|xy>");CHECK(text.letters.size()==4 && text.letters[2].small && text.letters[2].y==1);++layout_cases;
    for(const auto& bad:std::array<std::string,8>{"<S11:A>","<C17:A>","<F0:A>","<Rxx|>","<Qx>","A\\q","<Axx",std::string("\x81",1)}){rejects([&]{t::layout(bad);});++layout_cases;}
    CHECK(t::speaker_name(0)==std::string_view("\x90\xcb\x8d\xef",4) && t::speaker_style(50)==6 && t::speaker_style(-1)==9);++layout_cases;
}
int main(int argc,char** argv){try{
    CHECK(argc==3);layout_checks();const auto root=std::filesystem::path(argv[1]);
    s::Library library(r::Archive::open(p::open_resource_file(root/"ck-gal.pak")));
    for(unsigned index:{130u,131u,133u,134u,144u}){
        const auto& function=library.functions().at(index);std::cout<<index<<' '<<function.name<<" parameters "<<function.parameters.size()<<'\n';
        CHECK(function.parameters.size()==(index==134?0:index==144?1:3));
    }
    t::FontBook fonts(r::Archive::open(p::open_resource_file(root/"fon.pak")));
    auto prepared=fonts.prepare(t::layout(t::speaker_name(0)));CHECK(prepared.image_bytes()>0 && prepared.image_bytes()<256*1024 && fonts.source_bytes()<25*1024*1024);++font_cases;
    std::vector<i::Sprite> parts;prepared.append(parts,0,100000,0,0);CHECK(parts.size()==4 && parts[0].image==parts[3].image);++font_cases;
    const auto& pixels=parts[0].image->rgba;std::size_t coverage=0,intermediate=0;for(std::size_t n=3;n<pixels.size();n+=4){coverage+=pixels[n]>0;intermediate+=pixels[n]>0 && pixels[n]<255;}
    CHECK(coverage>100 && intermediate>0);++font_cases;
    parts.clear();prepared.append(parts,0,0,0,0);CHECK(parts.empty());prepared.append(parts,0,10,0,0);CHECK(!parts.empty() && parts.back().alpha==16);++font_cases;
    const auto raw_bytes=fonts.source_bytes();prepared=fonts.prepare(t::layout(t::speaker_name(1)));CHECK(fonts.source_bytes()==raw_bytes);++font_cases;
    i::ImageStore images(r::Archive::open(p::open_resource_file(root/"grp.pak")));
    s::SceneDialogue dialogue(fonts,[&](auto name){return images.request(name);});
    std::array<s::Value,1> name{s::Value(0)};s::Command command{144,name};dialogue.dispatch(command);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!dialogue.ready() && std::chrono::steady_clock::now()<deadline){dialogue.update();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    CHECK(dialogue.dispatch(command)->complete && dialogue.name()==t::speaker_name(0));++state_cases;
    std::array<s::Value,3> message{s::Value(std::string("\x90\xcb\x8d\xef",4)),s::Value(0),s::Value(1)};
    command={130,message};CHECK(!dialogue.dispatch(command)->complete);for(int n=0;n<60;++n)dialogue.update();
    CHECK(dialogue.phase()==s::DialoguePhase::waiting && !dialogue.dispatch(command)->complete);++state_cases;
    dialogue.advance();CHECK(dialogue.dispatch(command)->complete);++state_cases;
    message[0]=s::Value(std::string("AB\\kCD"));command={130,message};dialogue.dispatch(command);for(int n=0;n<60;++n)dialogue.update();
    dialogue.advance();CHECK(dialogue.page()==1 && dialogue.phase()==s::DialoguePhase::typing);dialogue.advance();CHECK(!dialogue.complete());dialogue.advance();CHECK(dialogue.complete());++state_cases;
    message[0]=s::Value(std::string("XY"));message[2]=s::Value(0);command={130,message};dialogue.dispatch(command);CHECK(dialogue.page()==2);++state_cases;
    CHECK(SDL_Init(0)==0);std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)> surface(SDL_CreateRGBSurfaceWithFormat(0,128,72,32,SDL_PIXELFORMAT_RGBA32),SDL_FreeSurface);CHECK(surface);
    {auto canvas=p::SdlCanvas::adopt(SDL_CreateSoftwareRenderer(surface.get()));canvas->logical_size(1280,720);p::SdlScene display(*canvas);
        for(int n=0;n<60;++n)dialogue.update();canvas->clear({0,0,0,255});display.sprites(dialogue.sprites());canvas->present();
        auto uploads=canvas->statistics().uploads;CHECK(uploads>=5);++render_cases;
        display.sprites(dialogue.sprites());CHECK(canvas->statistics().uploads==uploads);++render_cases;
    }
    surface.reset();SDL_Quit();
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.native-dialogue.v1\",\"layout_cases\":"<<layout_cases<<",\"font_cases\":"<<font_cases<<",\"state_cases\":"<<state_cases
        <<",\"render_cases\":"<<render_cases<<",\"font_source_bytes\":"<<fonts.source_bytes()<<",\"screen_access\":false,\"windows_created\":0,\"game_launched\":false,\"cpu_emulator_linked\":false,\"passed\":true}\n";
    std::cout<<"PASS native dialogue: "<<layout_cases<<" layout, "<<font_cases<<" atlas, "<<state_cases<<" state, "<<render_cases<<" owned-memory render checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
