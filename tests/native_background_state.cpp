#include "scene_background.hpp"
#include "scene_colour.hpp"
#include "resource_catalog.hpp"
#include "checkpoint_codec.hpp"
#include <bit>
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
void put(std::vector<std::uint8_t>& b,std::size_t at,std::uint32_t value){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(value>>(n*8));}
std::shared_ptr<Bytes> fixture(){auto source=std::make_shared<Bytes>();auto& b=source->b;b.resize(104);std::memcpy(b.data(),"KCAP",4);put(b,12,2);
    for(unsigned n=0;n<2;++n){const auto at=b.size();std::memcpy(b.data()+20+n*44,n?"mask.tga":"image.tga",n?8:9);put(b,52+n*44,std::uint32_t(at));put(b,56+n*44,82);b.resize(at+82);b[at+2]=2;b[at+12]=b[at+14]=4;b[at+16]=32;b[at+17]=40;
        for(unsigned p=0;p<16;++p){b[at+18+p*4+2]=255;b[at+18+p*4+3]=n?128:255;}}
    return source;}
std::shared_ptr<const i::Raster> await(const std::shared_ptr<i::ImageRequest>& request){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(std::chrono::steady_clock::now()<end){if(auto image=request->get())return image;std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("Synthetic background image wait expired");}
std::vector<std::uint8_t> patch(std::vector<std::uint8_t> b,std::size_t at,unsigned value){put(b,at,value);put(b,b.size()-4,wa2::checkpoint::crc(std::span(b).first(b.size()-4)));return b;}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},changed{},resolved{};auto source=fixture();r::Catalog resources([&](auto name)->std::shared_ptr<r::Source>{return name=="bak.pak" || name=="grp.pak"?source:nullptr;});
    const auto loader=[&](auto){return resources.image("bak","image.tga");};const auto masks=[&](auto){return resources.image("grp","mask.tga");};const s::SceneBackground::Resolve resolve=[&](const auto& ref){++resolved;return await(resources.image(ref));};
    s::SceneBackground background(loader,[&](bool){++changed;},masks,loader),restored(loader,[&](bool){++changed;},masks,loader);
    const auto initial=background.checkpoint(resources);restored.restore(initial,resources,resolve);CHECK(restored.checkpoint(resources)==initial && !changed && !resolved);++cases;
    std::array<s::Value,9> args{s::Value(1),s::Value(0),s::Value(0),s::Value(1),s::Value(0),s::Value(0),s::Value(0),s::Value(1280.f),s::Value(720.f)};s::Command command{146,args};background.dispatch(command,0);await(resources.image("bak","image.tga"));
    const auto pending=background.checkpoint(resources);restored.restore(pending,resources,resolve);CHECK(restored.checkpoint(resources)==pending && changed==1);background.update();restored.update();CHECK(background.checkpoint(resources)==restored.checkpoint(resources) && !background.busy());++cases;
    const auto compare=[&](unsigned ticks){const auto saved=background.checkpoint(resources);const auto before=changed;restored.restore(saved,resources,resolve);CHECK(restored.checkpoint(resources)==saved && changed==before);
        for(unsigned n=0;n<ticks;++n){background.update();restored.update();CHECK(background.checkpoint(resources)==restored.checkpoint(resources));const auto a=background.frame(),b=restored.frame();CHECK(bool(a.transition)==bool(b.transition) && bool(a.mask)==bool(b.mask));if(a.transition)CHECK(a.transition->previous.transform==b.transition->previous.transform && a.transition->current.transform==b.transition->current.transform);}};
    compare(1);++cases;
    for(auto effect:{11,12,13,14,16,17,18,19,0x1980}){args[0]=s::Value(effect);args[3]=s::Value(8);args[4]=s::Value(3);command={146,args};background.dispatch(command,0);await(resources.image("grp","mask.tga"));background.update();CHECK(!background.loading() && background.busy());compare(3);
        for(unsigned n=0;n<8;++n){background.update();restored.update();CHECK(background.checkpoint(resources)==restored.checkpoint(resources));}CHECK(!background.busy());++cases;}
    std::array<s::Value,5> pan{s::Value(40),s::Value(-20),s::Value(12),s::Value(3),s::Value(0)};command={210,pan};background.dispatch(command,0);for(unsigned n=0;n<3;++n)background.update();compare(5);++cases;
    args[0]=s::Value(13);args[1]=s::Value(-2);args[3]=s::Value(8);args[5]=s::Value(10000);args[6]=s::Value(10000);command={146,args};background.dispatch(command,0);const auto reuse=background.checkpoint(resources);restored.restore(reuse,resources,resolve);CHECK(restored.checkpoint(resources)==reuse);compare(9);++cases;
    args[0]=s::Value(0x1980);args[1]=s::Value(0);command={146,args};background.dispatch(command,0);restored.restore(background.checkpoint(resources),resources,resolve);background.update([](auto){return 0;});restored.update([](auto){return 0;});CHECK(!background.busy() && background.checkpoint(resources)==restored.checkpoint(resources));++cases;
    const auto stable=background.checkpoint(resources);auto corrupt=stable;corrupt.back()^=1;rejects([&]{background.restore(corrupt,resources,resolve);});CHECK(background.checkpoint(resources)==stable);++guards;
    for(const auto [at,value]:std::array<std::pair<unsigned,unsigned>,5>{{{12,2},{20,0},{28,15},{44,0x7fc00000},{56,2}}}){const auto before=resolved;rejects([&]{background.restore(patch(stable,at,value),resources,resolve);});CHECK(background.checkpoint(resources)==stable && resolved==before);++guards;}
    rejects([&]{background.restore(stable,resources,{});});CHECK(background.checkpoint(resources)==stable);++guards;
    rejects([&]{background.restore(stable,resources,[](const auto&)->std::shared_ptr<const i::Raster>{return {};});});CHECK(background.checkpoint(resources)==stable);++guards;
    rejects([&]{background.restore(stable,resources,[&](const auto&){return await(resources.image("grp","mask.tga"));});});CHECK(background.checkpoint(resources)==stable);++guards;
    s::SceneColour colour,other;std::array<s::Value,5> flash{s::Value(5),s::Value(7),s::Value(255),s::Value(32),s::Value(160)};command={152,flash};colour.dispatch(command);for(unsigned n=0;n<3;++n)colour.update();const auto tone=colour.checkpoint();other.restore(tone);CHECK(other.checkpoint()==tone);++cases;
    for(unsigned n=0;n<20;++n){colour.update();other.update();CHECK(colour.checkpoint()==other.checkpoint());}CHECK(!colour.busy() && colour.channels()[0]==128);++cases;
    const auto unchanged=colour.checkpoint();rejects([&]{colour.restore(patch(unchanged,12,256));});CHECK(colour.checkpoint()==unchanged);++guards;
    rejects([&]{colour.restore(patch(unchanged,56,0xffffffff));});CHECK(colour.checkpoint()==unchanged);++guards;
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-background-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"external_image_references\":true,\"restore_callbacks_not_replayed\":true,\"synthetic_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS native background/camera/mask and colour fade checkpoints.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
