#include "scene_selection.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

namespace s=wa2::script;namespace r=wa2::resource;namespace p=wa2::progress;namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
struct Bytes:r::Source {
    std::vector<std::uint8_t> bytes;
    std::uint64_t size()const override{return bytes.size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{if(at>bytes.size() || out.size()>bytes.size()-at)throw r::Error("Selection fixture bounds");if(!out.empty())std::memcpy(out.data(),bytes.data()+at,out.size());}
};
struct Storage:p::Storage {std::optional<p::Bytes> load()override{return {};}void write(p::Block,std::span<const std::uint8_t>)override{}};
void put(std::vector<std::uint8_t>& b,std::size_t at,std::uint32_t v){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(v>>(8*n));}
void half(std::vector<std::uint8_t>& b,std::size_t at,unsigned v){b.at(at)=std::uint8_t(v);b.at(at+1)=std::uint8_t(v>>8);}
r::Archive fixture(){auto result=std::make_shared<Bytes>();auto& b=result->bytes;b.resize(104);std::memcpy(b.data(),"KCAP",4);put(b,12,2);
    for(unsigned n=0;n<2;++n){const auto width=n?784u:248u,height=n?216u:104u;const auto start=b.size();const auto size=18+width*height*4;
        std::memcpy(b.data()+20+n*44,n?"sys_00200.tga":"sys_00011.tga",13);put(b,52+n*44,std::uint32_t(start));put(b,56+n*44,size);b.resize(start+size);
        b[start+2]=2;half(b,start+12,width);half(b,start+14,height);b[start+16]=32;b[start+17]=40;std::fill(b.begin()+start+18,b.end(),255);}
    return r::Archive::open(result);}
s::CommandResult invoke(s::SceneChoices& choices,unsigned id,std::initializer_list<s::Value> args){std::vector<s::Value> values(args);s::Command call{id,values};return *choices.dispatch(call);}
void entry(s::SceneChoices& choices,const char* text,int flag=0,int expected=0,bool disabled=false){invoke(choices,208,{s::Value(std::string(text)),s::Value(flag),s::Value(expected),s::Value(int(disabled))});}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},prepared{};i::ImageStore images(fixture());auto progress=std::make_shared<p::Store>(std::make_shared<Storage>());progress->state().set_flag(992,1);
    std::array<unsigned,3> sounds{};s::SceneChoices choices(progress,[]{return "1001";});s::SceneSelection view([&](auto name){return images.request(name);},[&](auto raw){CHECK(!raw.empty());++prepared;return wa2::text::PreparedText{};},[&](auto sound){++sounds[unsigned(sound)];});
    entry(choices,"first");entry(choices,"hidden",1,1);entry(choices,"disabled",2,1,true);entry(choices,"last");invoke(choices,209,{s::Value(0)});view.sync(choices);
    CHECK(view.rows().size()==3 && view.rows()[0].ordinal==0 && view.rows()[1].ordinal==2 && view.rows()[2].ordinal==3 && view.rows()[0].chosen && !view.ready());++cases;
    CHECK(view.rows()[0].y==208 && view.rows()[1].y==292 && view.rows()[2].y==376 && !view.rows()[1].enabled);++cases;
    CHECK(!view.input({true,true,false,400,220,0}));++cases;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);do{view.update();if(!view.loading())break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<deadline);
    CHECK(!view.loading() && view.opacity()==8 && !view.ready());++cases;
    for(unsigned tick=2;tick<=30;++tick){view.update();CHECK(view.opacity()==(tick==30?255:tick*255/30) && view.ready()==(tick==30));}++cases;
    const auto count=prepared;view.sync(choices);view.sync(choices);CHECK(prepared==count);++cases;
    auto sprites=view.sprites();CHECK(sprites.size()==7 && sprites[0].x==248 && sprites[0].y==194 && sprites[0].source_y==144 && sprites[0].blend==i::SpriteBlend::add && sprites[1].source_y==0 && sprites.back().x==328 && sprites.back().source_x==208 && sprites.back().layer==0x1058);++cases;
    CHECK(!view.input({true,false,false,400,300,0}) && view.focused()==2 && sounds[0]==0);CHECK(!view.input({true,false,false,248,220,0}) && !view.focused());++cases;
    CHECK(view.input({true,true,false,400,390,0})==3 && view.focused()==3);++cases;
    sprites=view.sprites();CHECK(sprites.size()==8);++cases;
    CHECK(!view.input({false,false,false,0,0,1}) && view.focused()==0);CHECK(!view.input({false,false,false,0,0,1}) && view.focused()==2);CHECK(!view.input({false,false,false,0,0,1}) && view.focused()==3);++cases;
    CHECK(!view.input({false,false,false,0,0,-1}) && view.focused()==2 && !view.input({false,false,true,0,0,0}) && sounds[2]==1);
    CHECK(!view.input({false,false,false,0,0,-1}) && view.focused()==0 && view.input({false,false,true,0,0,0})==0);++cases;
    view.replay_mode(true);CHECK(view.input({true,true,false,400,220,0})==0);CHECK(!view.input({true,true,false,400,390,0}) && sounds[2]==2 && choices.active());view.replay_mode(false);++cases;
    const auto before=view.rows().size();rejects([&]{view.input({true,false,false,std::numeric_limits<float>::quiet_NaN(),0,0});});rejects([&]{view.input({false,false,false,0,0,2});});CHECK(view.rows().size()==before);guards+=2;
    s::SelectionSettings bad;bad.skin=10;rejects([&]{view.settings(bad);});++guards;
    rejects([&]{s::SceneSelection missing({},[](auto){return wa2::text::PreparedText{};});});++guards;
    choices.choose(*view.input({true,true,false,400,390,0}));CHECK((progress->state().flag(992)&9)==9);view.sync(choices);CHECK(view.rows().empty() && view.sprites().empty() && !view.ready());++cases;
    for(unsigned n:{1u,2u,8u}){s::SceneChoices separate(progress,[]{return "1001";});for(unsigned row=0;row<n;++row)entry(separate,"fixture");invoke(separate,209,{s::Value(0)});view.sync(separate);
        CHECK(view.rows().size()==n && view.rows().front().y==334-int(n)*42 && view.rows().back().y==334-int(n)*42+int(n-1)*84);
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);do{view.update([](auto){return 0;});if(!view.loading())break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<until);
        CHECK(view.ready() && view.opacity()==255);CHECK(view.input({false,false,false,0,0,-1})==std::nullopt && view.focused()==0);++cases;
    }
    rejects([&]{choices.previously_chosen(8);});++guards;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-selection-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"synthetic_only\":true,\"font_preparation_stubbed\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";out.close();CHECK(out);
    std::cout<<"PASS native selection layout, focus, fade and original ordinals.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
