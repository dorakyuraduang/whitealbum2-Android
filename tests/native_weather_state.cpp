#include "scene_weather.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

namespace s=wa2::script;namespace r=wa2::resource;namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
struct Bytes:r::Source {std::vector<std::uint8_t> bytes;std::uint64_t size()const override{return bytes.size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{if(at>bytes.size() || out.size()>bytes.size()-at)throw r::Error("Weather fixture bounds");if(!out.empty())std::memcpy(out.data(),bytes.data()+std::size_t(at),out.size());}};
void put(std::vector<std::uint8_t>& b,std::size_t at,std::uint32_t v){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(v>>(8*n));}
std::shared_ptr<const i::Animation> animation(){
    std::vector<std::uint8_t> b(36+64+8+16);put(b,0,0x53414e49);put(b,16,1);put(b,20,60);put(b,24,1);put(b,28,1);put(b,32,1);
    put(b,36,0x80808001);put(b,36+20,1);put(b,60,0x80808001);put(b,60+20,0xfffffffe);put(b,60+28,32|(64<<16));put(b,60+32,0xfff0|(0xffe0u<<16));
    put(b,100,1);put(b,104,1);put(b,108,1);put(b,112,1);constexpr char name[]="weather.tga";b.insert(b.end(),name,name+sizeof(name));return i::Animation::parse(b);
}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},random_calls{};
    auto source=std::make_shared<Bytes>();auto& bytes=source->bytes;bytes.resize(60+18+32*64*3);std::memcpy(bytes.data(),"KCAP",4);put(bytes,12,1);
    std::memcpy(bytes.data()+20,"weather.tga",11);put(bytes,52,60);put(bytes,56,std::uint32_t(bytes.size()-60));bytes[62]=2;bytes[72]=32;bytes[74]=64;bytes[76]=24;bytes[77]=32;
    i::ImageStore images(r::Archive::open(source));auto request=images.request("weather.tga");std::shared_ptr<const i::Raster> image;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!(image=request->get()) && std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));CHECK(image);
    std::weak_ptr<const i::Animation> last_animation;
    s::SceneWeather weather([&](auto name){CHECK(name=="weather.tga");return request;},[&]{++random_calls;return 0;},[&](auto name){CHECK(name==std::string_view("\x89\x4a" "640x360.ani"));auto data=animation();last_animation=data;return data;});
    std::array<s::Value,7> args{s::Value(262),s::Value(3),s::Value(7),s::Value(100),s::Value(2),s::Value(1),s::Value(5)};s::Command call{229,args};
    CHECK(!weather.dispatch(call)->complete);weather.update();CHECK(weather.dispatch(call)->complete && weather.type()==6 && weather.extent()==80);++cases;
    const auto& first=weather.particles()[0];CHECK(first.phase==1 && first.kind==0 && std::abs(first.velocity-.0007f)<.000001f && std::abs(first.growth-1.0007f)<.000001f);++cases;
    CHECK(weather.sprites().size()==80 && weather.sprites()[0].source_y==0 && weather.sprites()[0].layer==0x1043 && weather.sprites()[0].width==1.25f);++cases;
    for(int n=0;n<100;++n){weather.update();CHECK(weather.extent()<=100);const auto live=std::count_if(weather.particles().begin(),weather.particles().end(),[](const auto& p){return p.active;});CHECK(std::size_t(live)==weather.extent());}++cases;
    std::array<s::Value,6> change{s::Value(-1000),s::Value(-1000),s::Value(1),s::Value(-1000),s::Value(-1000),s::Value(8)};call={230,change};CHECK(weather.dispatch(call)->complete);weather.update();++cases;
    weather.stop(true);CHECK(!weather.active() && weather.extent()==0);++cases;
    std::array<s::Value,6> snow{s::Value(0x703),s::Value(0),s::Value(10),s::Value(10),s::Value(2),s::Value(0)};random_calls=0;call={173,snow};weather.dispatch(call);weather.update();
    CHECK(weather.extent()==8 && random_calls==8*15 && weather.particles()[0].kind==248 && weather.particles()[0].rotation==.5f);++cases;
    snow[0]=s::Value(0x103);random_calls=0;call={173,snow};weather.dispatch(call);weather.update();CHECK(random_calls==8*9 && weather.particles()[0].kind==0);++cases;
    snow[0]=s::Value(0x303);random_calls=0;call={173,snow};weather.dispatch(call);weather.update();CHECK(random_calls==8*12 && weather.particles()[0].kind==192);++cases;
    snow[0]=s::Value(0x104);snow[2]=s::Value(10000);call={173,snow};weather.dispatch(call);weather.update();CHECK(weather.type()==4 && weather.extent()==8);++cases;
    call={175,{}};CHECK(!weather.dispatch(call)->complete);for(unsigned n=0;n<400 && weather.active();++n)weather.update();CHECK(weather.dispatch(call)->complete && weather.extent()==0);++cases;
    snow[0]=s::Value(0x803);call={173,snow};rejects([&]{weather.dispatch(call);});CHECK(!weather.active());++guards;
    snow[0]=s::Value(7);call={173,snow};rejects([&]{weather.dispatch(call);});++guards;
    change[3]=s::Value(3);call={230,change};rejects([&]{weather.dispatch(call);});++guards;
    snow[0]=s::Value(0);snow[2]=s::Value(0);random_calls=0;call={173,snow};CHECK(!weather.dispatch(call)->complete);weather.update();CHECK(weather.dispatch(call)->complete && weather.type()==0 && weather.sprites().size()==1 && random_calls==0);++cases;
    const auto rain=weather.sprites()[0];CHECK(rain.x==608 && rain.y==296 && rain.width==64 && rain.height==128 && rain.layer==0x1043 && rain.order==0x51);++cases;
    for(int n=0;n<5;++n)weather.update();CHECK(weather.sprites()[0].x==608);call={175,{}};CHECK(weather.dispatch(call)->complete && !weather.active());++cases;
    CHECK(last_animation.expired());snow[5]=s::Value(2);call={173,snow};weather.dispatch(call);weather.update();CHECK(!last_animation.expired());
    snow[0]=s::Value(0x103);call={173,snow};weather.dispatch(call);weather.update();CHECK(last_animation.expired() && weather.type()==3);++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-weather-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"implemented_types\":[0,3,4,6],\"synthetic_only\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";out.close();CHECK(out);
    std::cout<<"PASS native weather population, growth and original RNG consumption.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
