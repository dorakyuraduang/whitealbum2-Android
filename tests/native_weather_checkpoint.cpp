#include "scene_weather.hpp"
#include "resource_catalog.hpp"
#include "checkpoint_codec.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>

namespace s=wa2::script;namespace r=wa2::resource;namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
using Bytes=std::vector<std::uint8_t>;
void put(Bytes& b,std::size_t at,unsigned value){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(value>>(8*n));}
struct Source:r::Source {Bytes b;std::uint64_t size()const override{return b.size();}void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{CHECK(at<=b.size() && out.size()<=b.size()-at);std::copy_n(b.data()+at,out.size(),out.data());}};
std::shared_ptr<Source> fixture(){auto source=std::make_shared<Source>();auto& b=source->b;b.resize(16+44*4);std::memcpy(b.data(),"KCAP",4);put(b,12,4);constexpr std::string_view names[]{"sakura1_2.tga","sakura2.tga","sakura3.tga","weather.tga"};constexpr unsigned widths[]{1536,384,192,32},heights[]{256,64,32,64};
    for(unsigned n=0;n<4;++n){const auto at=b.size();const auto size=18+widths[n]*heights[n]*4;std::memcpy(b.data()+20+n*44,names[n].data(),names[n].size());put(b,52+n*44,unsigned(at));put(b,56+n*44,size);b.resize(at+size);b[at+2]=2;b[at+12]=std::uint8_t(widths[n]);b[at+13]=std::uint8_t(widths[n]>>8);b[at+14]=std::uint8_t(heights[n]);b[at+15]=std::uint8_t(heights[n]>>8);b[at+16]=32;b[at+17]=40;for(unsigned p=0;p<widths[n]*heights[n];++p){b[at+18+p*4]=std::uint8_t(31+n);b[at+19+p*4]=std::uint8_t(57+n);b[at+20+p*4]=std::uint8_t(191+n);b[at+21+p*4]=255;}}
    return source;}
std::shared_ptr<const i::Animation> animation(bool changed=false){Bytes b(36+64+8+24);put(b,0,0x53414e49);b[7]=changed;put(b,16,1);put(b,20,60);put(b,24,1);put(b,28,1);put(b,32,1);put(b,36,0x80808001);put(b,56,1);put(b,60,0x80808001);put(b,80,0xfffffffe);put(b,88,32|(64<<16));put(b,92,0xffe0fff0);put(b,100,1);put(b,104,2);put(b,108,1);put(b,112,2);put(b,116,1);put(b,120,3);constexpr char name[]="weather.tga";b.insert(b.end(),name,name+sizeof(name));return i::Animation::parse(b);}
std::shared_ptr<const i::Raster> await(const std::shared_ptr<i::ImageRequest>& request){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(std::chrono::steady_clock::now()<end){if(auto image=request->get())return image;std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("Synthetic weather checkpoint wait expired");}
Bytes patch(Bytes b,unsigned at,unsigned value){put(b,at,value);put(b,b.size()-4,wa2::checkpoint::crc(std::span(b).first(b.size()-4)));return b;}
s::CommandResult invoke(s::SceneWeather& weather,unsigned id,std::initializer_list<s::Value> args){std::vector<s::Value> values(args);s::Command command{id,values};return *weather.dispatch(command);}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},loads{},random_calls{},restore_random_calls{};bool changed{};int chroma=256;auto source=fixture();r::Catalog resources([&](auto name)->std::shared_ptr<r::Source>{return name=="grp.pak"?source:nullptr;});wa2::Random random(1),restored_random(91);
    const auto loader=[&](auto name){++loads;return resources.image("grp",name,chroma);};const auto animations=[&](auto name){CHECK(name==std::string_view("\x89\x4a" "640x360.ani"));return animation(changed);};const i::Resolve resolve=[&](const auto& ref){return await(resources.image(ref));};s::SceneWeather weather(loader,[&]{++random_calls;return random.next();},animations),restored(loader,[&]{++restore_random_calls;return restored_random.next();},animations);
    std::set<std::pair<const i::Raster*,const i::Raster*>> checked_images;
    const auto compare_sprites=[&]{const auto a=weather.sprites(),b=restored.sprites();CHECK(a.size()==b.size());for(unsigned n=0;n<a.size();++n){const auto& x=a[n];const auto& y=b[n];CHECK(x.x==y.x && x.y==y.y && x.width==y.width && x.height==y.height && x.source_x==y.source_x && x.source_y==y.source_y && x.source_width==y.source_width && x.source_height==y.source_height && x.alpha==y.alpha && x.layer==y.layer && x.order==y.order && x.world==y.world);if(checked_images.emplace(x.image.get(),y.image.get()).second)CHECK(x.image->width==y.image->width && x.image->height==y.image->height && x.image->rgba==y.image->rgba);}};
    const auto compare=[&](unsigned ticks){const auto saved=weather.checkpoint(resources);const auto reads=loads,draws=restore_random_calls;restored.restore(saved,resources,resolve);CHECK(restored.checkpoint(resources)==saved && loads==reads && restore_random_calls==draws);restored_random.seed(random.state());compare_sprites();for(unsigned n=0;n<ticks;++n){weather.update();restored.update();CHECK(random.state()==restored_random.state() && weather.checkpoint(resources)==restored.checkpoint(resources));compare_sprites();}};
    compare(0);++cases;
    for(int type:{0x103,0x104,0x106,0xf03,0x101}){invoke(weather,229,{s::Value(type),s::Value(3),s::Value(7),s::Value(10),s::Value(2),s::Value(1),s::Value(5)});compare(0);CHECK(weather.loading());++cases;constexpr std::string_view names[]{"weather.tga","sakura1_2.tga","sakura2.tga","sakura3.tga"};for(auto name:names)await(resources.image("grp",name,chroma));weather.update();compare(23);CHECK(weather.extent()>0);++cases;}
    invoke(weather,230,{s::Value(-5),s::Value(4),s::Value(3),s::Value(1),s::Value(2),s::Value(12)});weather.update();compare(23);++cases;weather.stop(false);compare(9);++cases;
    invoke(weather,173,{s::Value(0),s::Value(0),s::Value(0),s::Value(10),s::Value(0),s::Value(0)});CHECK(weather.sprites().empty());compare(0);++cases;weather.update();compare(17);CHECK(weather.sprites().size()==1);const auto rain=weather.checkpoint(resources);++cases;
    changed=true;rejects([&]{weather.restore(rain,resources,resolve);});CHECK(weather.checkpoint(resources)==rain);changed=false;++guards;
    chroma=128;invoke(weather,173,{s::Value(0x103),s::Value(0),s::Value(10),s::Value(10),s::Value(0),s::Value(2)});chroma=0;compare(0);await(resources.image("grp","weather.tga",128));weather.update();compare(9);++cases;
    // Scope 2 preserves an effect once; replacing it still clears stale draws.
    chroma=64;invoke(weather,173,{s::Value(0x101),s::Value(0),s::Value(10),s::Value(10),s::Value(0),s::Value(0)});CHECK(weather.sprites().empty());compare(0);for(auto name:{"sakura1_2.tga","sakura2.tga","sakura3.tga"})await(resources.image("grp",name,64));weather.update();compare(7);++cases;
    const auto stable=weather.checkpoint(resources);auto corrupt=stable;corrupt.back()^=1;rejects([&]{weather.restore(corrupt,resources,resolve);});CHECK(weather.checkpoint(resources)==stable);++guards;
    for(const auto [at,value]:std::array<std::pair<unsigned,unsigned>,9>{{{12,401},{20,5},{24,0x800},{28,3},{32,3},{36,401},{56,0x7fc00000},{64,2},{12,0}}}){rejects([&]{weather.restore(patch(stable,at,value),resources,resolve);});CHECK(weather.checkpoint(resources)==stable);++guards;}
    rejects([&]{weather.restore(stable,resources,{});});++guards;rejects([&]{weather.restore(stable,resources,[](const auto&)->std::shared_ptr<const i::Raster>{return {};});});++guards;
    weather.stop(true);compare(1);CHECK(!weather.active() && weather.sprites().empty());++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-weather-checkpoint.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"restore_random_calls\":0,\"same_frame_geometry\":true,\"atlas_regenerated\":true,\"synthetic_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS native weather snapshots, RNG continuation, atlases and same-frame geometry.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
