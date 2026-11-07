#include "scene_bitmaps.hpp"
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
using Bytes=std::vector<std::uint8_t>;
void put(Bytes& b,std::size_t at,unsigned value){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(value>>(8*n));}
struct Source:r::Source {Bytes b;std::uint64_t size()const override{return b.size();}void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{CHECK(at<=b.size() && out.size()<=b.size()-at);std::copy_n(b.data()+at,out.size(),out.data());}};
std::shared_ptr<Source> fixture(){auto source=std::make_shared<Source>();auto& b=source->b;b.resize(60+18+32*16*4);std::memcpy(b.data(),"KCAP",4);put(b,12,1);std::memcpy(b.data()+20,"atlas.tga",9);put(b,52,60);put(b,56,unsigned(b.size()-60));b[62]=2;b[72]=32;b[74]=16;b[76]=32;b[77]=40;for(unsigned n=0;n<512;++n){b[78+n*4]=31;b[79+n*4]=57;b[80+n*4]=191;b[81+n*4]=255;}return source;}
std::shared_ptr<const i::Animation> animation(bool changed=false,unsigned pieces=1){Bytes b(36);put(b,0,0x53414e49);put(b,16,1);put(b,20,60);put(b,24,2);put(b,28,1);put(b,32,1);b[7]=changed;
    for(unsigned n=0;n<2;++n){auto at=b.size();b.resize(at+24+pieces*40);put(b,at,0x80808001);put(b,at+20,pieces);at+=24;for(unsigned piece=0;piece<pieces;++piece,at+=40){put(b,at,0x80808001);put(b,at+20,0xfffffffe);put(b,at+24,n*16);put(b,at+28,16|(16<<16));put(b,at+32,0xfff80000|std::uint16_t(int(piece)-8));}}
    auto at=b.size();b.resize(at+8+24);put(b,at,1);put(b,at+4,2);put(b,at+8,1);put(b,at+12,2);put(b,at+16,1|(1<<16));put(b,at+20,1);constexpr char name[]="atlas.tga";b.insert(b.end(),name,name+sizeof(name));return i::Animation::parse(b);}
std::shared_ptr<const i::Raster> await(const std::shared_ptr<i::ImageRequest>& request){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(std::chrono::steady_clock::now()<end){if(auto image=request->get())return image;std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("Synthetic bitmap wait expired");}
s::CommandResult invoke(s::SceneBitmaps& bitmap,unsigned id,std::initializer_list<s::Value> args){std::vector<s::Value> values(args);s::Command command{id,values};return *bitmap.dispatch(command);}
Bytes patch(Bytes b,unsigned at,unsigned value){put(b,at,value);put(b,b.size()-4,wa2::checkpoint::crc(std::span(b).first(b.size()-4)));return b;}
void compare_sprites(std::span<const i::Sprite> a,std::span<const i::Sprite> b){CHECK(a.size()==b.size());for(unsigned n=0;n<a.size();++n){const auto& x=a[n];const auto& y=b[n];CHECK(x.x==y.x && x.y==y.y && x.width==y.width && x.height==y.height && x.source_x==y.source_x && x.source_y==y.source_y && x.source_width==y.source_width && x.source_height==y.source_height && x.red==y.red && x.green==y.green && x.blue==y.blue && x.alpha==y.alpha && x.angle==y.angle && x.pivot_x==y.pivot_x && x.pivot_y==y.pivot_y && x.flip==y.flip && x.blend==y.blend && x.layer==y.layer && x.order==y.order && x.world==y.world && x.image->rgba==y.image->rgba);}}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},loads{},animation_loads{};bool changed{};int chroma=256;auto source=fixture();r::Catalog resources([&](auto name)->std::shared_ptr<r::Source>{return name=="grp.pak"?source:nullptr;});
    const auto loader=[&](auto name,auto group,auto){++loads;return resources.image(group,name,chroma);};const auto animations=[&](auto name,auto group){CHECK(name=="animation.ani" && group=="grp");++animation_loads;return animation(changed);};const i::Resolve resolve=[&](const auto& ref){return await(resources.image(ref));};s::SceneBitmaps bitmaps(loader,animations),restored(loader,animations);
    restored.restore(bitmaps.checkpoint(resources),resources,resolve);CHECK(bitmaps.checkpoint(resources)==restored.checkpoint(resources) && !loads && !animation_loads);++cases;
    invoke(bitmaps,176,{s::Value(3),s::Value(std::string("atlas.tga")),s::Value(14),s::Value(1),s::Value(std::string()),s::Value(256),s::Value(std::string("grp")),s::Value(0)});
    const auto pending=bitmaps.checkpoint(resources);chroma=0;const auto count=loads;restored.restore(pending,resources,resolve);CHECK(restored.loading() && restored.checkpoint(resources)==pending && loads==count);await(resources.image("grp","atlas.tga",256));bitmaps.update();restored.update();CHECK(bitmaps.checkpoint(resources)==restored.checkpoint(resources));++cases;
    const auto compare=[&](unsigned ticks){const auto saved=bitmaps.checkpoint(resources);const auto reads=loads;restored.restore(saved,resources,resolve);CHECK(restored.checkpoint(resources)==saved && reads==loads);compare_sprites(bitmaps.sprites(),restored.sprites());for(unsigned n=0;n<ticks;++n){bitmaps.update();restored.update();CHECK(bitmaps.checkpoint(resources)==restored.checkpoint(resources));compare_sprites(bitmaps.sprites(),restored.sprites());}};
    invoke(bitmaps,191,{s::Value(3),s::Value(10),s::Value(20),s::Value(2.f)});
    invoke(bitmaps,188,{s::Value(3),s::Value(30),s::Value(40)});CHECK(bitmaps.slot(3).pivot_x==10 && bitmaps.slot(3).pivot_y==20);
    CHECK(bitmaps.sprites()[0].x==50 && bitmaps.sprites()[0].y==60);compare(0);++cases;
    invoke(bitmaps,189,{s::Value(3),s::Value(65539),s::Value(-65543),s::Value(1),s::Value(2),s::Value(16),s::Value(12)});
    CHECK(bitmaps.slot(3).x==65539 && bitmaps.slot(3).y==-65543 && bitmaps.slot(3).pivot_x==10 && bitmaps.slot(3).pivot_y==20);compare(0);++cases;
    invoke(bitmaps,190,{s::Value(3),s::Value(13),s::Value(17),s::Value(65537),s::Value(-65539)});
    CHECK(bitmaps.slot(3).width==65537 && bitmaps.slot(3).height==-65539 && bitmaps.slot(3).pivot_x==10 && bitmaps.slot(3).pivot_y==20);compare(0);++cases;
    invoke(bitmaps,189,{s::Value(3),s::Value(-12),s::Value(7),s::Value(1),s::Value(2),s::Value(16),s::Value(12)});invoke(bitmaps,190,{s::Value(3),s::Value(-8),s::Value(13),s::Value(32),s::Value(24)});
    invoke(bitmaps,186,{s::Value(3),s::Value(3)});invoke(bitmaps,187,{s::Value(3),s::Value(88),s::Value(111),s::Value(128)});invoke(bitmaps,191,{s::Value(3),s::Value(640),s::Value(360),s::Value(2.5f)});invoke(bitmaps,192,{s::Value(3),s::Value(640),s::Value(360),s::Value(512)});invoke(bitmaps,185,{s::Value(3),s::Value(3),s::Value(20),s::Value(12)});bitmaps.update();compare(15);++cases;
    for(int mode:{1,3,4,6}){invoke(bitmaps,185,{s::Value(3),s::Value(mode),s::Value(0),s::Value(0)});compare(1);CHECK(bitmaps.sprites().size()==unsigned(mode==4 || mode==6));++cases;}
    invoke(bitmaps,183,{s::Value(3),s::Value(0)});compare(1);CHECK(bitmaps.sprites().empty());++cases;invoke(bitmaps,183,{s::Value(3),s::Value(1)});
    bitmaps.retire_background();invoke(bitmaps,176,{s::Value(3),s::Value(std::string("atlas.tga")),s::Value(14),s::Value(1),s::Value(std::string()),s::Value(256),s::Value(std::string("grp")),s::Value(0)});await(resources.image("grp","atlas.tga",chroma));bitmaps.update();compare(2);CHECK(bitmaps.sprites().size()==2);++cases;
    bitmaps.clear_retired();restored.clear_retired();CHECK(bitmaps.checkpoint(resources)==restored.checkpoint(resources) && bitmaps.sprites().size()==1);++cases;
    invoke(bitmaps,177,{s::Value(7),s::Value(std::string("animation.ani")),s::Value(16),s::Value(std::string()),s::Value(256),s::Value(std::string("grp")),s::Value(1)});compare(0);CHECK(bitmaps.loading());++cases;bitmaps.update();compare(15);CHECK(bitmaps.sprites().size()==2 && bitmaps.slot(7).animation_validated);++cases;
    invoke(bitmaps,190,{s::Value(7),s::Value(100),s::Value(200),s::Value(640),s::Value(360)});compare(0);
    {const auto& draw=bitmaps.sprites().back();CHECK(draw.x==92 && draw.y==192 && draw.width==16 && draw.height==16 && draw.pivot_x==100 && draw.pivot_y==200 && draw.source_width==16 && draw.source_height==16);}++cases;
    invoke(bitmaps,189,{s::Value(7),s::Value(-20),s::Value(30),s::Value(999),s::Value(-777),s::Value(0),s::Value(-10)});compare(2);
    {const auto& draw=bitmaps.sprites().back();CHECK(draw.x==-28 && draw.y==22 && draw.width==16 && draw.height==16 && draw.source_y==0 && draw.source_width==16 && draw.source_height==16);}++cases;
    invoke(bitmaps,191,{s::Value(7),s::Value(0),s::Value(0),s::Value(2.f)});compare(0);
    {const auto& draw=bitmaps.sprites().back();CHECK(draw.x==-56 && draw.y==44 && draw.width==32 && draw.height==32 && draw.pivot_x==0 && draw.pivot_y==0);}++cases;
    invoke(bitmaps,188,{s::Value(7),s::Value(65539),s::Value(-65543)});compare(0);
    {const auto& draw=bitmaps.sprites().back();CHECK(draw.x==65523 && draw.y==-65559 && draw.width==32 && draw.height==32 && draw.pivot_x==65539 && draw.pivot_y==-65543);}++cases;
    invoke(bitmaps,192,{s::Value(7),s::Value(65537),s::Value(-65539),s::Value(1073741824)});compare(0);
    {const auto& draw=bitmaps.sprites().back();CHECK(draw.angle==0 && draw.pivot_x==65537 && draw.pivot_y==-65539);}++cases;
    bitmaps.retire_background();compare(3);CHECK(!bitmaps.slot(7).retiring && bitmaps.slot(3).retiring);++cases;
    bitmaps.clear_retired();restored.clear_retired();compare(0);CHECK(bitmaps.sprites().size()==1);const auto stable=bitmaps.checkpoint(resources);++cases;
    auto corrupt=stable;corrupt.back()^=1;rejects([&]{bitmaps.restore(corrupt,resources,resolve);});CHECK(bitmaps.checkpoint(resources)==stable);++guards;
    for(const auto [at,value]:std::array<std::pair<unsigned,unsigned>,10>{{{12,33},{16,0x7fc00000},{48,0xbf800000},{68,5},{72,256},{88,40000},{92,32},{96,129},{108,4},{116,2}}}){rejects([&]{bitmaps.restore(patch(stable,at,value),resources,resolve);});CHECK(bitmaps.checkpoint(resources)==stable);++guards;}
    rejects([&]{bitmaps.restore(stable,resources,{});});++guards;rejects([&]{bitmaps.restore(stable,resources,[](const auto&)->std::shared_ptr<const i::Raster>{return {};});});++guards;
    changed=true;rejects([&]{bitmaps.restore(stable,resources,resolve);});CHECK(bitmaps.checkpoint(resources)==stable);changed=false;++guards;
    invoke(bitmaps,180,{s::Value(7)});compare(1);CHECK(bitmaps.sprites().empty());++cases;
    s::SceneBitmaps dense(loader,[](auto,auto){return animation(false,32);});invoke(dense,177,{s::Value(0),s::Value(std::string("animation.ani")),s::Value(16),s::Value(std::string()),s::Value(256),s::Value(std::string("grp")),s::Value(1)});await(resources.image("grp","atlas.tga",chroma));dense.update();const auto pieces=dense.sprites();CHECK(pieces.size()==32);for(unsigned n=0;n<pieces.size();++n)CHECK(pieces[n].x==int(n)-8 && pieces[n].order==0x11 && pieces[n].layer==0x1045);++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-bitmap-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"animation_progress_restored\":true,\"pending_styles_preserved\":true,\"retired_layers_restored\":true,\"synthetic_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS native bitmap transforms, fades, animation and retired-layer checkpoints.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
