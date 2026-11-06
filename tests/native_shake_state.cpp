#include "scene_shake.hpp"
#include "scene_dialogue.hpp"
#include "native_random.hpp"
#include <fstream>
#include <iostream>
#include <cstring>

namespace s=wa2::script;namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
struct EmptyPak:wa2::resource::Source {
    std::uint64_t size()const override{return 16;}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{constexpr std::array<std::uint8_t,16> bytes{'K','C','A','P'};CHECK(at<=bytes.size() && out.size()<=bytes.size()-at);std::copy_n(bytes.data()+at,out.size(),out.data());}
};
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
    wa2::text::FontBook fonts(wa2::resource::Archive::open(std::make_shared<EmptyPak>()));
    const auto no_images=[](auto)->std::shared_ptr<i::ImageRequest>{throw std::runtime_error("EndMessage must not request images");};
    s::SceneDialogue dialogue(fonts,no_images,{},[&]{shake.end_message();});
    std::array<s::Value,1> ignored{s::Value(123)};s::Command end{132,ignored};
    for(auto duration:{60,-1,-2}){invoke(shake,150,{s::Value(513),s::Value(32),s::Value(duration),s::Value(2),s::Value(120)});shake.update();CHECK(shake.active() && !shake.world().identity());
        const auto reply=dialogue.dispatch(end);CHECK(reply && reply->complete && !reply->yield_frame && reply->value->integer()==1 && !shake.active() && shake.world().identity() && shake.dialogue().identity());++cases;}
    invoke(shake,150,{s::Value(513),s::Value(32),s::Value(0),s::Value(2),s::Value(120)});shake.update();const auto continuous=shake.checkpoint();dialogue.dispatch(end);CHECK(shake.active() && shake.checkpoint()==continuous);++cases;
    CHECK(invoke(shake,151,{}).complete && !shake.active() && shake.world().identity() && shake.dialogue().identity());dialogue.dispatch(end);CHECK(!shake.active());++cases;
    rejects([&]{s::SceneDialogue unbound(fonts,no_images);unbound.dispatch(end);});++guards;
    rejects([&]{invoke(shake,151,{s::Value(1)});});++guards;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-shake-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"synthetic_only\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";out.close();CHECK(out);
    std::cout<<"PASS native shake modes, scopes, RNG order and checkpoints.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
