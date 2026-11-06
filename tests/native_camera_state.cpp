#include "scene_camera.hpp"
#include "scene_background.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

namespace s=wa2::script;namespace r=wa2::resource;namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
bool near(float a,float b){return std::abs(a-b)<.001f;}
s::CommandResult invoke(s::SceneCamera& camera,unsigned id,std::initializer_list<s::Value> args,unsigned bank=0){
    std::vector<s::Value> values(args);s::Command call{id,values};return *camera.dispatch(call,bank);
}
struct Bytes:r::Source {
    std::vector<std::uint8_t> data;
    std::uint64_t size()const override{return data.size();}
    void read_at(std::uint64_t offset,std::span<std::uint8_t> out)override{
        if(offset>data.size() || out.size()>data.size()-offset)throw r::Error("Camera fixture bounds");
        if(!out.empty())std::memcpy(out.data(),data.data()+std::size_t(offset),out.size());
    }
};
void put(std::vector<std::uint8_t>& b,std::size_t at,std::uint32_t v){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(v>>(8*n));}
r::Archive fixture(){
    auto bytes=std::make_shared<Bytes>();auto& b=bytes->data;b.resize(60+27);std::memcpy(b.data(),"KCAP",4);put(b,12,1);
    std::memcpy(b.data()+20,"B000000.tga",11);put(b,52,60);put(b,56,27);
    b[62]=2;b[72]=3;b[74]=1;b[76]=24;b[77]=32;return r::Archive::open(bytes);
}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};s::SceneCamera camera;
    camera.bind(0,1280,720,{});camera.bind(1,640,360,{320,180});
    CHECK(!camera.busy() && invoke(camera,213,{}).complete);++cases;
    for(int easing=0;easing<=4;++easing){
        camera.bind(0,1280,720,{});invoke(camera,210,{s::Value(100),s::Value(200),s::Value(4),s::Value(easing),s::Value(0)});
        const float quarter[]{.25f,.0625f,.4375f,.125f,.375f};camera.update();CHECK(near(camera.pose(0).x,100*quarter[easing]));
        CHECK(!invoke(camera,213,{}).complete);camera.update();camera.update();camera.update();CHECK(!camera.busy() && near(camera.pose(0).y,200));++cases;
    }
    camera.bind(0,1280,720,{});invoke(camera,210,{s::Value(101),s::Value(-101),s::Value(4),s::Value(0),s::Value(0)});camera.update();
    CHECK(near(camera.pose(0).x,25.25f));invoke(camera,210,{s::Value(101),s::Value(-101),s::Value(4),s::Value(0),s::Value(0)});camera.update();
    CHECK(near(camera.pose(0).x,44.f) && near(camera.pose(0).y,-44.f));++cases;
    invoke(camera,214,{s::Value(1),s::Value(2)});camera.update();CHECK(near(camera.pose(0).x,44.f));camera.update();CHECK(near(camera.pose(0).x,63.f));++cases;
    invoke(camera,211,{s::Value(3.f),s::Value(5.f),s::Value(4),s::Value(0),s::Value(100),s::Value(200),s::Value(0)});
    invoke(camera,212,{s::Value(90.f),s::Value(4),s::Value(0),s::Value(100),s::Value(200),s::Value(0)});
    invoke(camera,210,{s::Value(80),s::Value(40),s::Value(4),s::Value(0),s::Value(0)},1);camera.update();
    CHECK(near(camera.pose(0).scale_x,1.5f) && near(camera.pose(0).scale_y,2.f) && near(camera.pose(0).angle,22.5f));
    CHECK(near(camera.pose(1).x,340.f) && near(camera.pose(1).y,190.f));++cases;
    const auto saved=camera.checkpoint();s::SceneCamera restored;restored.restore(saved);CHECK(restored.checkpoint()==saved);
    for(int n=0;n<5;++n){camera.update();restored.update();CHECK(camera.checkpoint()==restored.checkpoint());}++cases;
    auto corrupt=saved;corrupt.back()^=1;const auto before=restored.checkpoint();rejects([&]{restored.restore(corrupt);});CHECK(restored.checkpoint()==before);++guards;
    rejects([&]{restored.bind(2,1,1,{});});rejects([&]{restored.bind(0,0,1,{});});rejects([&]{restored.bind(0,1,1,{0,0,0,1});});guards+=3;
    rejects([&]{invoke(restored,211,{s::Value(0.f),s::Value(1.f),s::Value(1),s::Value(0),s::Value(0),s::Value(0),s::Value(0)});});CHECK(restored.checkpoint()==before);++guards;
    camera.bind(0,1280,720,{});invoke(camera,210,{s::Value(100),s::Value(0),s::Value(120),s::Value(0),s::Value(0)});
    camera.update([](auto){return 0;});CHECK(!camera.busy() && camera.pose(0).x==100);++cases;
    for(int preset:{-1,-2}){camera.bind(0,1280,720,{});invoke(camera,210,{s::Value(30),s::Value(0),s::Value(preset),s::Value(0),s::Value(0)});
        for(int n=0;n<(preset==-1?15:30)-1;++n)camera.update();CHECK(camera.busy());camera.update();CHECK(!camera.busy());++cases;}
    i::ImageStore images(fixture());auto request=images.request("B000000.tga");std::shared_ptr<const i::Raster> raster;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!(raster=request->get()) && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));CHECK(raster);
    std::vector<bool> keep;unsigned loads{};std::string requested;
    const auto loader=[&](std::string_view name){++loads;requested=name;return request;};s::SceneBackground backgrounds(loader,[&](bool value){keep.push_back(value);},loader,loader);
    std::array<s::Value,9> basic{s::Value(1),s::Value(0),s::Value(0),s::Value(1),s::Value(0),s::Value(0),s::Value(0),s::Value(2560.f),s::Value(2160.f)};
    s::Command call{146,basic};CHECK(!backgrounds.dispatch(call,0)->complete);backgrounds.update();CHECK(backgrounds.dispatch(call,0)->complete);
    auto frame=backgrounds.frame();CHECK(frame.current && frame.current->width==6 && frame.current->height==3 && frame.current->x==636 && frame.current->y==357);CHECK(!keep.back());++cases;
    std::array<s::Value,11> ext;std::copy(basic.begin(),basic.end(),ext.begin());ext[7]=s::Value(2.f);ext[8]=s::Value(3.f);ext[9]=s::Value(0.f);ext[10]=s::Value(1);
    call={226,ext};backgrounds.dispatch(call,0);backgrounds.update();CHECK(backgrounds.dispatch(call,0)->complete && keep.back());
    frame=backgrounds.frame();CHECK(frame.current->x==636 && frame.current->width==6);++cases;
    ext[3]=s::Value(4);ext[5]=s::Value(10000);ext[6]=s::Value(10000);ext[7]=s::Value(10000.f);ext[8]=s::Value(10000.f);ext[10]=s::Value(0);
    call={225,ext};CHECK(!backgrounds.dispatch(call,0)->complete);backgrounds.update();CHECK(backgrounds.dispatch(call,0)->complete && backgrounds.busy());
    frame=backgrounds.frame();CHECK(frame.current->x==636 && frame.current->width==6 && frame.current->opacity==63);++cases;
    const auto old_loads=loads;ext[9]=s::Value(std::numeric_limits<float>::quiet_NaN());call={225,ext};rejects([&]{backgrounds.dispatch(call,0);});CHECK(loads==old_loads);++guards;
    ext[9]=s::Value(45.f);ext[3]=s::Value(1);call={227,ext};backgrounds.dispatch(call,0);backgrounds.update();
    frame=backgrounds.frame();CHECK(requested=="v000000.tga" && backgrounds.picture() && frame.current->angle==45 && frame.current->pivot_x==640);++cases;
    std::array<s::Value,5> pan{s::Value(30),s::Value(20),s::Value(1),s::Value(0),s::Value(0)};call={210,pan};CHECK(backgrounds.dispatch(call,0)->complete);backgrounds.update();
    CHECK(backgrounds.camera().pose(0).x==668 || backgrounds.camera().pose(1).x==668);++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-camera-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"synthetic_only\":true,\"screen_access\":false,\"game_launched\":false,\"passed\":true}\n";out.close();CHECK(out);
    std::cout<<"PASS native camera interpolation, state restore and background parameter conventions.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
