// Synthetic image requests and scene state only; no original assets/device.
#include "scene_background.hpp"
#include "scene_bitmaps.hpp"
#include "scene_characters.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

namespace s=wa2::script;namespace r=wa2::resource;namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
struct Bytes:r::Source {
    std::vector<std::uint8_t> bytes;
    std::uint64_t size()const override{return bytes.size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{if(at>bytes.size() || out.size()>bytes.size()-at)throw r::Error("World fixture bounds");if(!out.empty())std::memcpy(out.data(),bytes.data()+at,out.size());}
};
void put(std::vector<std::uint8_t>& b,std::size_t at,std::uint32_t v){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(v>>(8*n));}
r::Archive fixture(){auto source=std::make_shared<Bytes>();auto& b=source->bytes;b.resize(60+21);std::memcpy(b.data(),"KCAP",4);put(b,12,1);
    std::memcpy(b.data()+20,"synthetic.tga",13);put(b,52,60);put(b,56,21);b[62]=2;b[72]=1;b[74]=1;b[76]=24;b[77]=32;return r::Archive::open(source);}
template<class Component>s::CommandResult invoke(Component& component,unsigned id,std::initializer_list<s::Value> args){std::vector<s::Value> values(args);s::Command call{id,values};return *component.dispatch(call);}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};i::ImageStore images(fixture());auto request=images.request("synthetic.tga");std::shared_ptr<const i::Raster> raster;
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!(raster=request->get()) && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));CHECK(raster);
    for(auto world:{i::SpriteWorld::both,i::SpriteWorld::previous,i::SpriteWorld::current}){
        CHECK(i::in_world(world,true)==(world!=i::SpriteWorld::current) && i::in_world(world,false)==(world!=i::SpriteWorld::previous));++cases;
    }
    s::SceneCharacters characters([&](auto){return request;});
    CHECK(!invoke(characters,155,{s::Value(10),s::Value(1111),s::Value(1),s::Value(0),s::Value(255),s::Value(128)}).complete);characters.update();
    CHECK(characters.sprites().size()==1 && characters.sprites()[0].world==i::SpriteWorld::both);const auto old_x=characters.sprites()[0].x;++cases;
    characters.retire_background();CHECK(characters.sprites().size()==1 && characters.sprites()[0].world==i::SpriteWorld::previous);++cases;
    CHECK(invoke(characters,155,{s::Value(10),s::Value(1111),s::Value(80.f),s::Value(0),s::Value(255),s::Value(128)}).complete);characters.update();
    auto layers=characters.sprites();CHECK(characters.count()==1 && layers.size()==2 && layers[0].x==old_x && layers[1].x==old_x+80 && layers[0].world==i::SpriteWorld::previous && layers[1].world==i::SpriteWorld::both && layers[0].order==layers[1].order);++cases;
    characters.clear_retired();CHECK(characters.sprites().size()==1 && characters.sprites()[0].x==old_x+80);++cases;
    // Reasserting an identical character still transfers it to the new world.
    characters.retire_background();invoke(characters,155,{s::Value(10),s::Value(1111),s::Value(80.f),s::Value(0),s::Value(255),s::Value(128)});characters.update();
    CHECK(characters.sprites().size()==2);characters.clear_retired();CHECK(characters.count()==1);++cases;
    characters.retire_background();characters.clear_retired();CHECK(!characters.count() && characters.sprites().empty());++cases;
    s::SceneBitmaps bitmaps([&](auto,auto,auto){return request;});
    const auto load=[&](bool independent){return invoke(bitmaps,176,{s::Value(7),s::Value(std::string("synthetic.tga")),s::Value(14),s::Value(1),s::Value(std::string()),s::Value(256),s::Value(std::string("grp")),s::Value(int(independent))});};
    CHECK(!load(false).complete);bitmaps.update();CHECK(bitmaps.sprites().size()==1 && bitmaps.sprites()[0].world==i::SpriteWorld::current && bitmaps.sprites()[0].order==0x18);++cases;
    bitmaps.retire_background();CHECK(bitmaps.sprites()[0].world==i::SpriteWorld::previous);CHECK(!load(false).complete);bitmaps.update();
    layers=bitmaps.sprites();CHECK(layers.size()==2 && layers[0].world==i::SpriteWorld::previous && layers[1].world==i::SpriteWorld::current && layers[0].order==0x18 && layers[1].order==0x18);++cases;
    bitmaps.clear_retired();CHECK(bitmaps.slot(7).allocated && bitmaps.sprites().size()==1);++cases;
    load(true);bitmaps.update();bitmaps.retire_background();bitmaps.clear_retired();CHECK(bitmaps.slot(7).allocated && bitmaps.sprites()[0].layer==0x1043);++cases;
    bitmaps.clear();CHECK(bitmaps.sprites().empty());++cases;
    unsigned requests{};s::SceneBackground backgrounds([&](auto){++requests;return request;});
    std::array<s::Value,9> args{s::Value(1),s::Value(0),s::Value(0),s::Value(1),s::Value(0),s::Value(0),s::Value(0),s::Value(1280.f),s::Value(720.f)};
    s::Command call{146,args};backgrounds.dispatch(call,0);backgrounds.update();CHECK(!backgrounds.frame().transition);++cases;
    for(int effect:{12,17}){
        args[0]=s::Value(effect);args[3]=s::Value(60);call={146,args};CHECK(!backgrounds.dispatch(call,0)->complete);
        for(int tick=1;tick<=60;++tick){backgrounds.update();const auto frame=backgrounds.frame();
            if(tick==60){CHECK(!frame.previous && !frame.transition && frame.current->opacity==255);continue;}
            CHECK(frame.previous && frame.current && frame.transition);
            if(effect==12){const auto t=float(tick)/60;CHECK(frame.transition->previous.transform.scale==2-(1-t)*(1-t) && frame.transition->previous.opacity==255 && frame.transition->current.opacity==tick*255/60);
                if(tick==15)CHECK(frame.transition->previous.transform.scale==1.4375f);if(tick==30)CHECK(frame.transition->previous.transform.scale==1.75f);
            }else{const auto alpha=tick*255/60;CHECK(frame.transition->previous.opacity==alpha && frame.transition->current.opacity==alpha);
                if(tick==1)CHECK(frame.transition->previous.transform.y==-23 && frame.transition->current.transform.y==697);
                if(tick==15)CHECK(frame.transition->previous.transform.y==-311 && frame.transition->current.transform.y==409);
                if(tick==30)CHECK(frame.transition->previous.transform.y==-538 && frame.transition->current.transform.y==182);
                if(tick==59)CHECK(frame.transition->previous.transform.y==-720 && frame.transition->current.transform.y==0);
            }
        }CHECK(backgrounds.dispatch(call,0)->complete);++cases;
    }
    const auto before=requests;args[0]=s::Value(18);call={146,args};rejects([&]{backgrounds.dispatch(call,0);});CHECK(requests==before && !backgrounds.busy());++guards;
    args[0]=s::Value(12);call={146,args};backgrounds.dispatch(call,0);backgrounds.update([](auto){return 0;});CHECK(!backgrounds.frame().transition && !backgrounds.busy());++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-world-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"synthetic_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";out.close();CHECK(out);
    std::cout<<"PASS native world ownership and composite transition states.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
