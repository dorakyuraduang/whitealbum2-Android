#include "scene_novel.hpp"
#include "scene_dialogue.hpp"
#include "text_layout.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>

namespace s=wa2::script;namespace t=wa2::text;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};s::SceneNovel novel;novel.alpha(127);
    std::array<s::Value,2> args{s::Value(4),s::Value(1)};s::Command hide{233,args};CHECK(novel.dispatch(hide)->complete);++cases;
    std::array<s::Value,1> mode{s::Value(1)};s::Command enable{220,mode};CHECK(novel.dispatch(enable)->complete && novel.enabled() && novel.opacity()==127);++cases;
    hide={233,args};CHECK(!novel.dispatch(hide)->complete && novel.busy());novel.update();CHECK(novel.opacity()==96 && !novel.text_visible());++cases;
    const auto saved=novel.checkpoint();s::SceneNovel restored;restored.restore(saved);CHECK(restored.checkpoint()==saved);
    for(int n=0;n<4;++n){novel.update();restored.update();CHECK(novel.checkpoint()==restored.checkpoint());}CHECK(novel.opacity()==0 && novel.dispatch(hide)->complete);++cases;
    s::Command show{234,args};CHECK(!novel.dispatch(show)->complete);novel.update();CHECK(novel.text_visible() && novel.opacity()==31);++cases;
    for(int n=0;n<3;++n)novel.update();CHECK(novel.busy() && novel.opacity()==127);novel.update();CHECK(!novel.busy() && novel.dispatch(show)->complete);++cases;
    args[1]=s::Value(0);hide={233,args};CHECK(novel.dispatch(hide)->complete && novel.busy());novel.update([](auto){return 0;});CHECK(!novel.busy() && !novel.opacity());++cases;
    mode[0]=s::Value(0);enable={220,mode};novel.dispatch(enable);CHECK(!novel.enabled() && !novel.opacity());show={234,args};CHECK(novel.dispatch(show)->complete && !novel.busy());++cases;
    const auto before=novel.checkpoint();auto corrupt=saved;corrupt.back()^=128;rejects([&]{novel.restore(corrupt);});CHECK(novel.checkpoint()==before);++guards;
    rejects([&]{novel.alpha(256);});rejects([&]{novel.alpha(-1);});guards+=2;
    t::TextStyle style;style.columns=40;style.rows=15;std::string text;for(int n=0;n<11;++n)text+="AB\\n";
    const auto layout=t::layout(text,style);CHECK(layout.letters.size()==22 && layout.letters.back().y==434);++cases;
    rejects([&]{t::layout(text);});++guards;
    // Empty text never asks FontBook for an atlas. The controller integration
    // can therefore be checked without opening a resource or graphics device.
    t::FontBook fonts({});s::SceneDialogue dialogue(fonts,[](auto)->std::shared_ptr<wa2::image::ImageRequest>{throw std::runtime_error("Unexpected asset load");});
    s::DialogueSettings settings;settings.novel_alpha=127;dialogue.settings(settings);mode[0]=s::Value(1);enable={220,mode};dialogue.dispatch(enable);
    const auto sprites=dialogue.sprites();CHECK(sprites.size()==1 && sprites[0].width==1280 && sprites[0].height==720 && sprites[0].alpha==127 && sprites[0].image->rgba==std::vector<std::uint8_t>({0,0,0,255}));++cases;
    CHECK(sprites[0].layer==0x104c && sprites[0].order==0x41d && sprites[0].space==wa2::image::SpriteSpace::screen);++cases;
    auto invalid=settings;invalid.novel_alpha=256;rejects([&]{dialogue.settings(invalid);});CHECK(dialogue.novel().opacity()==127);++guards;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-novel-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"synthetic_only\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";out.close();CHECK(out);
    std::cout<<"PASS native novel backing, text geometry and fade continuation.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
