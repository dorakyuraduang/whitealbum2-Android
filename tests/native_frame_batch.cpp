#include "scene.hpp"
#include <fstream>
#include <iostream>

namespace g=wa2::game;namespace s=wa2::script;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},steps{},reads{};
    auto result=g::run_frame_batch([&](bool edge){CHECK(edge);++steps;return g::FrameTick{s::RunState::yielded,false};},[&]{++reads;return 0;});
    CHECK(result.ticks==1 && steps==1 && reads==1 && result.limit==g::FrameLimit::none);++cases;
    steps=0;std::int64_t time{};result=g::run_frame_batch([&](bool edge){CHECK(edge==(steps==0));++steps;return g::FrameTick{s::RunState::yielded,true};},[&]{return time++;},g::FrameBudget{3,100});
    CHECK(result.ticks==3 && steps==3 && result.limit==g::FrameLimit::time);++cases;
    steps=0;result=g::run_frame_batch([&](bool){++steps;return g::FrameTick{s::RunState::yielded,true};},[]{return 0;},g::FrameBudget{2000,4});
    CHECK(result.ticks==4 && steps==4 && result.limit==g::FrameLimit::ticks);++cases;
    steps=0;result=g::run_frame_batch([&](bool){++steps;return g::FrameTick{s::RunState::yielded,true};},[]{return 0;},g::FrameBudget{0,4});CHECK(result.ticks==1 && result.limit==g::FrameLimit::time);++cases;
    steps=0;result=g::run_frame_batch([&](bool){++steps;return g::FrameTick{s::RunState::yielded,steps<2};},[]{return 0;});CHECK(result.ticks==2 && result.limit==g::FrameLimit::none);++cases;
    for(auto state:{s::RunState::stopped,s::RunState::slice_exhausted}){steps=0;result=g::run_frame_batch([&](bool){++steps;return g::FrameTick{state,true};},[]{return 0;});CHECK(result.ticks==1 && result.state==state && steps==1 && result.limit==g::FrameLimit::none);++cases;}
    g::Input input;input.advance=input.skip=input.toggle_window=true;input.choice=7;input.selection={true,true,true,400,300,1};const auto continued=input.continued();steps=0;
    result=g::run_frame_batch([&](bool first){const auto& value=first?input:continued;CHECK(value.skip && value.selection.pointer && value.selection.x==400 && value.selection.y==300);
        CHECK(value.advance==first && value.toggle_window==first && value.choice==(first?7:-1) && value.selection.activate==first && value.selection.confirm==first && value.selection.navigation==(first?1:0));++steps;return g::FrameTick{s::RunState::yielded,steps<3};},[]{return 0;});
    CHECK(result.ticks==3 && result.limit==g::FrameLimit::none);++cases;
    result=g::run_frame_batch([](bool){return g::FrameTick{s::RunState::yielded,false};},[]{return 0;},g::FrameBudget{50000,4096});CHECK(result.ticks==1);++cases;
    for(auto budget:{g::FrameBudget{-1,1},g::FrameBudget{50001,1},g::FrameBudget{1,0},g::FrameBudget{1,4097}}){steps=0;rejects([&]{g::run_frame_batch([&](bool){++steps;return g::FrameTick{s::RunState::yielded,true};},[]{return 0;},budget);});CHECK(!steps);++guards;}
    steps=0;rejects([&]{g::run_frame_batch([&](bool){++steps;return g::FrameTick{s::RunState::yielded,true};},[]{return -1;});});CHECK(!steps);++guards;
    steps=0;time=10;rejects([&]{g::run_frame_batch([&](bool){++steps;return g::FrameTick{s::RunState::yielded,true};},[&]{return time--;});});CHECK(steps==1);++guards;
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-frame-batch.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"transient_input_once\":true,\"bounded_on_constant_clock\":true,\"fake_steps_and_clock_only\":true,\"scene_instances_created\":0,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS native skip frame budgets and input edges: "<<cases<<" cases.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
