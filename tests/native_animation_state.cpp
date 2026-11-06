#include "sprite_animation.hpp"
#include "scene_bitmaps.hpp"
#include <chrono>
#include <cstring>
#include <thread>
#include <fstream>
#include <iostream>

namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
using Bytes=std::vector<std::uint8_t>;
void put(Bytes& b,std::size_t at,std::uint32_t value){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(value>>(8*n));}
void half(Bytes& b,std::size_t at,std::uint16_t value){b.at(at)=std::uint8_t(value);b.at(at+1)=std::uint8_t(value>>8);}
Bytes fixture(bool nested=false){
    Bytes bytes(36);put(bytes,0,0x53414e49);put(bytes,16,0xcccccc01);put(bytes,20,60);put(bytes,24,2);put(bytes,28,1);put(bytes,32,1);
    for(unsigned n=0;n<2;++n){auto at=bytes.size();bytes.resize(at+64);put(bytes,at,0x80808001);put(bytes,at+12,0xcc00);put(bytes,at+20,1);
        at+=24;put(bytes,at,0x80808001);put(bytes,at+4,0x5500);put(bytes,at+8,0xcccc0000);put(bytes,at+16,0xcc00);put(bytes,at+20,0xfffffffe);
        half(bytes,at+24,std::uint16_t(n*16));half(bytes,at+28,16);half(bytes,at+30,16);half(bytes,at+32,std::uint16_t(-8));half(bytes,at+34,std::uint16_t(-8));}
    const std::vector<i::AnimationStep> steps=nested?std::vector<i::AnimationStep>{{2,1,0,0},{2,1,0,0},{1,0,1,0},{3,0,0,0},{3,0,0,0},{0,0,0,0}}:
        std::vector<i::AnimationStep>{{2,1,0,0},{1,0,2,0},{1,1,1,0},{3,0,0,0},{0,0,0,0}};
    auto at=bytes.size();bytes.resize(at+8);put(bytes,at,0x88888801);put(bytes,at+4,std::uint32_t(steps.size()-1));
    for(const auto& step:steps){at=bytes.size();bytes.resize(at+8);half(bytes,at,step.operation);half(bytes,at+2,step.argument);half(bytes,at+4,step.duration);half(bytes,at+6,step.event);}
    constexpr char name[]="atlas.tga";bytes.insert(bytes.end(),name,name+sizeof(name));return bytes;
}
struct Source:wa2::resource::Source {Bytes bytes;std::uint64_t size()const override{return bytes.size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{if(at>bytes.size() || out.size()>bytes.size()-at)throw wa2::resource::Error("Animation fixture bounds");if(!out.empty())std::memcpy(out.data(),bytes.data()+std::size_t(at),out.size());}};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};const auto bytes=fixture();auto data=i::Animation::parse(bytes);
    CHECK(data->rate()==60 && data->frames().size()==2 && data->textures()[0]=="atlas.tga" && data->frames()[1].pieces[0].source_x==16 && data->frames()[0].pieces[0].x==-8);++cases;
    auto empty=bytes;put(empty,36+24+28,0);auto empty_data=i::Animation::parse(empty);CHECK(empty_data->frames()[0].pieces[0].empty());i::AnimationPlayer empty_player(empty_data);empty_player.tick();CHECK(empty_player.frame()==0);++cases;
    auto editor=bytes;half(editor,164+8+2,0);half(editor,164+8+4,10);i::AnimationPlayer editor_player(i::Animation::parse(editor),0,1);
    for(int n=0;n<20;++n)editor_player.tick();CHECK(!editor_player.finished());++cases;
    i::AnimationPlayer player(data,0,1);CHECK(!player.frame());const unsigned expected[]{0,0,1,0,0,1};
    for(auto frame:expected){player.tick();CHECK(player.frame()==frame && !player.finished());}player.tick();CHECK(player.finished() && player.frame()==1);++cases;
    i::AnimationPlayer hidden(data,0,1,false);for(int n=0;n<7;++n)hidden.tick();CHECK(hidden.finished() && !hidden.frame());++cases;
    i::AnimationPlayer continuous(data),restored(data);continuous.tick();continuous.tick();const auto saved=continuous.checkpoint();restored.restore(saved);CHECK(restored.checkpoint()==saved);
    for(int n=0;n<100;++n){continuous.tick();restored.tick();CHECK(continuous.checkpoint()==restored.checkpoint());}CHECK(!continuous.finished());++cases;
    i::AnimationPlayer two(data,0,2);for(int n=0;n<12;++n)two.tick();CHECK(!two.finished());two.tick();CHECK(two.finished());++cases;
    i::AnimationPlayer slow(data);slow.tick(120);CHECK(!slow.frame());slow.tick(120);CHECK(slow.frame()==0);++cases;
    const auto slow_before=slow.checkpoint();rejects([&]{slow.tick(60);});CHECK(slow.checkpoint()==slow_before);++guards;
    i::AnimationPlayer fast(data,0,1);for(int n=0;n<4;++n)fast.tick(30);CHECK(fast.finished());i::AnimationPlayer fast_restore(data,0,1);fast_restore.restore(fast.checkpoint());CHECK(fast_restore.checkpoint()==fast.checkpoint());++cases;
    i::AnimationPlayer nested(i::Animation::parse(fixture(true)),0,1);for(int n=0;n<4;++n){nested.tick();CHECK(nested.frame()==0 && !nested.finished());}nested.tick();CHECK(nested.finished());++cases;
    auto corrupt=saved;corrupt.back()^=1;const auto before=restored.checkpoint();rejects([&]{restored.restore(corrupt);});CHECK(restored.checkpoint()==before);++guards;
    auto different=bytes;different[7]=12;i::AnimationPlayer mismatch(i::Animation::parse(different));rejects([&]{mismatch.restore(saved);});++guards;
    for(auto size:{0u,35u,60u,unsigned(bytes.size()-1)}){rejects([&]{i::Animation::parse(std::span(bytes).first(size));});++guards;}
    for(const auto [at,value]:std::vector<std::pair<unsigned,unsigned>>{{0,0},{20,0},{24,4097},{32,33},{36,0x80807f01},{36+24+20,0},{36+24+28,0x0010ffff},{164+8,9}}){auto bad=bytes;put(bad,at,value);rejects([&]{i::Animation::parse(bad);});++guards;}
    auto bad=bytes;half(bad,164+8+8+4,0);rejects([&]{i::Animation::parse(bad);});++guards;
    bad=bytes;bad.insert(bad.end(),0);rejects([&]{i::Animation::parse(bad);});++guards;
    auto source=std::make_shared<Source>();auto& image_bytes=source->bytes;image_bytes.resize(60+18+32*16*3);put(image_bytes,0,0x5041434b);put(image_bytes,12,1);std::memcpy(image_bytes.data()+20,"atlas.tga",9);put(image_bytes,52,60);put(image_bytes,56,std::uint32_t(image_bytes.size()-60));image_bytes[62]=2;image_bytes[72]=32;image_bytes[74]=16;image_bytes[76]=24;image_bytes[77]=32;
    i::ImageStore images(wa2::resource::Archive::open(source));auto request=images.request("atlas.tga");std::shared_ptr<const i::Raster> pixels;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!(pixels=request->get()) && std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));CHECK(pixels);
    namespace s=wa2::script;s::SceneBitmaps bitmaps([&](auto name,auto group,auto mode){CHECK(name=="atlas.tga" && group=="grp" && mode==1);return request;},[&](auto name,auto group){CHECK(name=="animation.ani" && group=="grp");return data;});
    std::array<s::Value,7> load{s::Value(0),s::Value(std::string("animation.ani")),s::Value(14),s::Value(std::string()),s::Value(256),s::Value(std::string("grp")),s::Value(1)};s::Command call{177,load};
    CHECK(!bitmaps.dispatch(call)->complete);bitmaps.update();CHECK(bitmaps.dispatch(call)->complete && bitmaps.sprites().size()==1 && bitmaps.sprites()[0].layer==0x1043);++cases;
    std::array<s::Value,3> move{s::Value(0),s::Value(639),s::Value(359)};call={188,move};bitmaps.dispatch(call);
    CHECK(bitmaps.slot(0).pivot_x==639 && bitmaps.slot(0).pivot_y==359);++cases;
    std::array<s::Value,4> zoom{s::Value(0),s::Value(640),s::Value(360),s::Value(8.f)};call={191,zoom};bitmaps.dispatch(call);
    CHECK(bitmaps.sprites()[0].x==568 && bitmaps.sprites()[0].y==288 && bitmaps.sprites()[0].width==128);++cases;
    bitmaps.update();CHECK(bitmaps.sprites()[0].source_x==0);bitmaps.update();CHECK(bitmaps.sprites()[0].source_x==16);++cases;
    std::array<s::Value,4> blend{s::Value(0),s::Value(3),s::Value(48),s::Value(0)};call={185,blend};bitmaps.dispatch(call);CHECK(bitmaps.sprites()[0].alpha==48 && bitmaps.sprites()[0].blend==i::SpriteBlend::add);++cases;
    std::array<s::Value,5> rectangle{s::Value(0),s::Value(1),s::Value(2),s::Value(3),s::Value(4)};call={190,rectangle};rejects([&]{bitmaps.dispatch(call);});CHECK(bitmaps.sprites()[0].x==568);++guards;
    bitmaps.retire_background();bitmaps.clear_retired();CHECK(bitmaps.slot(0).allocated);bitmaps.clear();CHECK(bitmaps.sprites().empty());++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-animation-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"synthetic_only\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";out.close();CHECK(out);
    std::cout<<"PASS native INAS parsing, sequence loops and playback checkpoints.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
