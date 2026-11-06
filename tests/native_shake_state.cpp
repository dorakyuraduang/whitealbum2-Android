#include "scene_shake.hpp"
#include "native_random.hpp"
#include <fstream>
#include <iostream>

namespace s=wa2::script;namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
s::CommandResult invoke(s::SceneShake& shake,unsigned id,std::initializer_list<s::Value> args){std::vector<s::Value> values(args);s::Command call{id,values};return *shake.dispatch(call);}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};wa2::Random random;s::SceneShake shake([&]{return random.next();});
    CHECK(!shake.active() && shake.world().identity() && shake.dialogue().identity());++cases;
    // Numeric expectations come from constructed-data calls into cached
    // 00401d10/0040c740. They contain no original assets or rendered pixels.
    constexpr std::array<int,8> sine{-10,-20,-26,-28,-27,-22,-15,-5},square{32,32,32,-32,-32,-32,-32,32},rotation{168,336,144,328,152,336,160,360};
    for(int mode:{0,1,2,12,13,15,256,513}){
        CHECK(!invoke(shake,150,{s::Value(mode),s::Value(32),s::Value(60),s::Value(2),s::Value(120)}).complete);
        for(unsigned tick=1;tick<=61;++tick){shake.update();const auto world=shake.world(),dialogue=shake.dialogue();CHECK(shake.active()==(tick<60));
            if(tick>=60){CHECK(world.identity() && dialogue.identity());continue;}
            if(tick>8)continue;const auto n=tick-1;
            if(mode==0)CHECK(world.x==sine[n] && world.y==0 && dialogue.identity());
            if(mode==1)CHECK(world.x==square[n] && world.y==0 && dialogue.identity());
            if(mode==2)CHECK(world.scale==float(1.+double(tick*tick)/256.) && dialogue.identity());
            if(mode==12)CHECK(world.angle==rotation[n] && world.scale==1 && dialogue.identity());
            if(mode==13)CHECK(world.angle==sine[n] && dialogue.identity());
            if(mode==15)CHECK(world.scale==float(1.+.2*double(tick*tick)/256.) && dialogue.identity());
            if(mode==256)CHECK(dialogue.x==sine[n] && dialogue.y==0 && world.identity());
            if(mode==513)CHECK(world.x==square[n] && world==dialogue);
        }++cases;
    }
    // Every signed direction, both finite and continuous sine envelopes.
    constexpr std::array<int,8> xs{0,-1,-1,-1,0,1,1,1},ys{1,1,0,-1,-1,-1,0,1};
    for(int direction=0;direction<8;++direction)for(int duration:{0,60}){
        CHECK(invoke(shake,150,{s::Value(0),s::Value(32),s::Value(duration),s::Value(direction),s::Value(120)}).complete==(duration==0));
        shake.update();const int amount=duration?10:11;CHECK(shake.world().x==amount*xs[direction] && shake.world().y==amount*ys[direction]);++cases;
    }
    for(int duration:{-1,-2}){invoke(shake,150,{s::Value(1),s::Value(10),s::Value(duration),s::Value(6),s::Value(8)});
        for(int tick=1;tick<(duration==-1?15:30);++tick){shake.update();CHECK(shake.active());}shake.update();CHECK(!shake.active());++cases;}
    invoke(shake,150,{s::Value(0),s::Value(32),s::Value(60),s::Value(2),s::Value(120)});shake.update([](auto){return 0;});CHECK(!shake.active() && shake.world().identity());++cases;
    CHECK(invoke(shake,231,{s::Value(0),s::Value(32),s::Value(60),s::Value(2),s::Value(120),s::Value(0)}).complete && shake.active());++cases;
    std::array<s::Value,6> params{s::Value(0),s::Value(32),s::Value(2),s::Value(2),s::Value(120),s::Value(1)};s::Command call{231,params};
    CHECK(!shake.dispatch(call)->complete);shake.update();CHECK(!shake.dispatch(call)->complete);shake.update();CHECK(shake.dispatch(call)->complete);++cases;
    invoke(shake,150,{s::Value(12),s::Value(10),s::Value(0),s::Value(2),s::Value(4096)});shake.update();CHECK(shake.world().angle==-.1f && shake.world().scale==1);++cases;
    invoke(shake,150,{s::Value(12),s::Value(10),s::Value(0),s::Value(3),s::Value(2048)});shake.update();CHECK(shake.world().angle==.1f && shake.world().scale==.5f);++cases;
    invoke(shake,150,{s::Value(13),s::Value(32),s::Value(0),s::Value(2),s::Value(120)});shake.update();CHECK(shake.world().angle==-11);++cases;
    const auto saved=shake.checkpoint();wa2::Random other_random;s::SceneShake restored([&]{return other_random.next();});restored.restore(saved);CHECK(restored.checkpoint()==saved);
    for(int tick=0;tick<300;++tick){shake.update();restored.update();CHECK(shake.checkpoint()==restored.checkpoint());}++cases;
    auto corrupt=saved;corrupt.back()^=1;const auto before=restored.checkpoint();rejects([&]{restored.restore(corrupt);});CHECK(restored.checkpoint()==before);++guards;
    for(const auto [mode,amplitude,duration,direction,speed]:std::array<std::array<int,5>,7>{{{3,1,60,2,1},{768,1,60,2,1},{0,-1,60,2,1},{2,0,60,2,1},{0,1,-3,2,1},{0,1,60,8,1},{0,1,60,2,-1}}}){
        rejects([&]{invoke(restored,150,{s::Value(mode),s::Value(amplitude),s::Value(duration),s::Value(direction),s::Value(speed)});});CHECK(restored.checkpoint()==before);++guards;
    }
    std::vector<unsigned> numbers{2,2,6,1,5};unsigned draws{};s::SceneShake randomized([&]{CHECK(draws<numbers.size());return numbers[draws++];});
    invoke(randomized,150,{s::Value(9),s::Value(16),s::Value(20),s::Value(2),s::Value(32)});randomized.update();CHECK(draws==3 && randomized.world().x==16 && randomized.world().y==0);
    randomized.update();CHECK(draws==3 && randomized.world().x==16);randomized.update();CHECK(draws==4 && randomized.world().x==-16 && randomized.world().y==16);++cases;
    invoke(randomized,231,{s::Value(521),s::Value(5),s::Value(30),s::Value(2),s::Value(32),s::Value(0)});randomized.update();
    CHECK(draws==4 && randomized.world().x==-5 && randomized.world()==randomized.dialogue());++cases;
    wa2::Random a(19);s::SceneShake a_shake([&]{return a.next();});invoke(a_shake,150,{s::Value(9),s::Value(16),s::Value(0),s::Value(2),s::Value(32)});a_shake.update();
    const auto a_saved=a_shake.checkpoint();wa2::Random b(a.state());s::SceneShake b_shake([&]{return b.next();});b_shake.restore(a_saved);
    for(int tick=0;tick<300;++tick){a_shake.update();b_shake.update();CHECK(a.state()==b.state() && a_shake.checkpoint()==b_shake.checkpoint());}++cases;
    b_shake.stop();s::SceneShake stopped([&]{return 0;});stopped.restore(b_shake.checkpoint());CHECK(!stopped.active());++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-shake-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"synthetic_only\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";out.close();CHECK(out);
    std::cout<<"PASS native shake modes, scopes, RNG order and checkpoints.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
