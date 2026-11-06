#include "scene_characters.hpp"
#include "resource_catalog.hpp"
#include "checkpoint_codec.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

namespace s=wa2::script;namespace r=wa2::resource;namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
struct Bytes:r::Source {std::vector<std::uint8_t> b;std::uint64_t size()const override{return b.size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{CHECK(at<=b.size() && out.size()<=b.size()-at);std::copy_n(b.data()+at,out.size(),out.data());}};
void put(std::vector<std::uint8_t>& b,std::size_t at,unsigned value){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(value>>(8*n));}
std::shared_ptr<Bytes> fixture(){auto result=std::make_shared<Bytes>();auto& b=result->b;b.resize(60+18+24);std::memcpy(b.data(),"KCAP",4);put(b,12,1);std::memcpy(b.data()+20,"image.tga",9);put(b,52,60);put(b,56,42);b[62]=2;b[72]=2;b[74]=3;b[76]=32;b[77]=40;
    for(unsigned n=0;n<6;++n){b[78+n*4]=17;b[79+n*4]=34;b[80+n*4]=211;b[81+n*4]=255;}return result;}
std::shared_ptr<const i::Raster> await(const std::shared_ptr<i::ImageRequest>& request){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(std::chrono::steady_clock::now()<deadline){if(auto value=request->get())return value;std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("Synthetic character image wait expired");}
s::CommandResult invoke(s::SceneCharacters& actor,unsigned id,std::initializer_list<s::Value> args){std::vector<s::Value> values(args);s::Command command{id,values};return *actor.dispatch(command);}
std::vector<std::uint8_t> patch(std::vector<std::uint8_t> b,unsigned at,unsigned value){put(b,at,value);put(b,b.size()-4,wa2::checkpoint::crc(std::span(b).first(b.size()-4)));return b;}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},loads{};int chroma=256;auto source=fixture();r::Catalog resources([&](auto name)->std::shared_ptr<r::Source>{return name=="char.pak"?source:nullptr;});
    const auto loader=[&](auto){++loads;return resources.image("char","image.tga",chroma);};const i::Resolve resolve=[&](const auto& ref){return await(resources.image(ref));};s::SceneCharacters characters(loader),restored(loader);
    restored.restore(characters.checkpoint(resources),resources,resolve);CHECK(restored.checkpoint(resources)==characters.checkpoint(resources) && !loads);++cases;
    invoke(characters,154,{s::Value(10),s::Value(1111),s::Value(1),s::Value(0),s::Value(1),s::Value(8),s::Value(200),s::Value(128)});const auto pending=characters.checkpoint(resources);chroma=0;
    const auto before=loads;restored.restore(pending,resources,resolve);CHECK(restored.checkpoint(resources)==pending && loads==before && restored.loading());await(resources.image("char","image.tga",256));characters.update();restored.update();CHECK(characters.checkpoint(resources)==restored.checkpoint(resources));++cases;
    const auto compare=[&](unsigned ticks){const auto saved=characters.checkpoint(resources);const auto reads=loads;restored.restore(saved,resources,resolve);CHECK(restored.checkpoint(resources)==saved && loads==reads);
        for(unsigned n=0;n<ticks;++n){characters.update();restored.update();CHECK(characters.checkpoint(resources)==restored.checkpoint(resources));const auto a=characters.sprites(),b=restored.sprites();CHECK(a.size()==b.size());
            for(unsigned p=0;p<a.size();++p)CHECK(a[p].x==b[p].x && a[p].y==b[p].y && a[p].width==b[p].width && a[p].height==b[p].height && a[p].alpha==b[p].alpha && a[p].world==b[p].world && a[p].order==b[p].order && a[p].image->rgba==b[p].image->rgba);}};
    compare(9);CHECK(!characters.busy(10));++cases;
    for(int effect:{0,1,2,3,6,7}){invoke(characters,154,{s::Value(10),s::Value(1120+effect),s::Value(40.f),s::Value(2),s::Value(effect),s::Value(12),s::Value(128),s::Value(64)});await(resources.image("char","image.tga",chroma));characters.update();compare(14);++cases;}
    characters.invalidate_image_style();chroma=128;invoke(characters,154,{s::Value(10),s::Value(1127),s::Value(-25.f),s::Value(2),s::Value(0),s::Value(12),s::Value(255),s::Value(128)});await(resources.image("char","image.tga",128));characters.update();CHECK(characters.sprites().size()==2);compare(3);++cases;
    characters.retire_background();invoke(characters,154,{s::Value(10),s::Value(1128),s::Value(10.f),s::Value(2),s::Value(0),s::Value(12),s::Value(255),s::Value(128)});characters.update();compare(3);CHECK(characters.sprites().size()>=2);characters.clear_retired();restored.clear_retired();CHECK(characters.checkpoint(resources)==restored.checkpoint(resources) && characters.count()==1);++cases;
    invoke(characters,156,{s::Value(10),s::Value(6),s::Value(12)});characters.update();compare(15);CHECK(!characters.count());++cases;
    invoke(characters,155,{s::Value(10),s::Value(1200),s::Value(1),s::Value(0),s::Value(255),s::Value(128)});await(resources.image("char","image.tga",chroma));characters.update();compare(1);CHECK(!characters.loading() && !characters.sprites().empty());const auto stable=characters.checkpoint(resources);++cases;
    auto corrupt=stable;corrupt.back()^=1;rejects([&]{characters.restore(corrupt,resources,resolve);});CHECK(characters.checkpoint(resources)==stable);++guards;
    for(const auto [at,value]:std::array<std::pair<unsigned,unsigned>,7>{{{20,9},{24,999},{28,4},{32,40000},{44,0x7fc00000},{48,2},{68,999}}}){rejects([&]{characters.restore(patch(stable,at,value),resources,resolve);});CHECK(characters.checkpoint(resources)==stable);++guards;}
    rejects([&]{characters.restore(stable,resources,{});});CHECK(characters.checkpoint(resources)==stable);++guards;
    rejects([&]{characters.restore(stable,resources,[](const auto&)->std::shared_ptr<const i::Raster>{return {};});});CHECK(characters.checkpoint(resources)==stable);++guards;
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-character-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"pending_style_preserved\":true,\"retired_layers_restored\":true,\"synthetic_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS native character transitions, styles and retired-layer checkpoints.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
