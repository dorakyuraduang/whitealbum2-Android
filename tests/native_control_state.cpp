#include "scene_builtins.hpp"
#include "scene_controls.hpp"
#include "scene_read_history.hpp"
#include "scalar_math.hpp"
#include "scene_movie.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>

namespace s=wa2::script;namespace p=wa2::progress;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
struct Storage final:p::Storage {
    std::optional<p::Bytes> bytes;unsigned writes{};
    std::optional<p::Bytes> load()override{return bytes;}
    void write(p::Block block,std::span<const std::uint8_t> data)override{if(!bytes)bytes=p::Bytes(p::layout::system_save_bytes);std::copy(data.begin(),data.end(),bytes->begin()+p::offset(block));++writes;}
};
struct Movie final:wa2::movie::Playback {
    bool done{},stopped{};
    bool finished()const override{return done;}
    void stop()override{done=stopped=true;}
    void update(std::int64_t)override{}
    void pause(bool,std::int64_t)override{}
    void gain(float)override{}
    std::optional<wa2::movie::VideoFrame> frame()const override{return {};}
};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};
    wa2::Random random(1),reference(1);unsigned printed{};s::SceneBuiltins builtins(random,[&](auto value){CHECK(value=="diagnostic");++printed;});
    const auto invoke=[&](unsigned id,std::initializer_list<s::Value> args){std::vector<s::Value> values(args);s::Command call{id,values};return builtins.dispatch(call);};
    for(auto bound:{-1,0}){CHECK(invoke(8,{s::Value(bound)})->value->integer()==0 && random.state()==1);++cases;}
    for(int n=1;n<=256;++n){CHECK(invoke(8,{s::Value(n)})->value->integer()==std::int32_t(reference.next()%unsigned(n)));++cases;}
    invoke(4,{s::Value(std::string("diagnostic"))});CHECK(printed==1);++cases;
    for(float angle:{-720.f,-360.f,-90.f,-0.f,0.f,30.f,45.f,90.f,180.f,360.f,720.f}){
        const auto input=float(double(angle)*0.01745329238474369);
        CHECK(std::bit_cast<unsigned>(invoke(9,{s::Value(angle)})->value->real())==std::bit_cast<unsigned>(float(wa2::math::sine_finite(input).value)));
        CHECK(std::bit_cast<unsigned>(invoke(10,{s::Value(angle)})->value->real())==std::bit_cast<unsigned>(float(wa2::math::cosine_finite(input).value)));++cases;
    }
    CHECK(invoke(17,{s::Value(9.f)})->value->real()==3.f);CHECK(invoke(16,{s::Value(2.f),s::Value(8.f)})->value->real()==256.f);++cases;
    rejects([&]{invoke(15,{s::Value(1.f)});});CHECK(!invoke(900,{}));++guards;
    auto disk=std::make_shared<Storage>();auto store=std::make_shared<p::Store>(disk);store->flush();s::SceneReadHistory reading(store);
    reading.enter("1001");CHECK(reading.script_index()==0 && reading.status()==0);reading.seen("1001",1);CHECK(store->state().read(0,1));
    store->state().mark_read(0,2);CHECK(reading.status()==1);++cases;
    reading.enter("1002");CHECK(reading.script_index()==1 && reading.last_line()==0 && reading.status()==0 && store->state().read(0,2));++cases;
    reading.force(2);CHECK(reading.status()==2);reading.force(-3);CHECK(reading.status()==0 && reading.status(true)==1);++cases;
    reading.enter("catch");CHECK(reading.script_index()==29);reading.enter("1002");CHECK(store->state().read(29,1));++cases;
    reading.seen("1002",100);CHECK(p::State::decode(*disk->bytes).read(1,100));const auto saved=reading.checkpoint();reading.enter("LF3");CHECK(reading.script_index()==0);
    reading.restore(saved);CHECK(reading.script_index()==1 && reading.last_line()==100 && reading.checkpoint()==saved);++cases;
    const auto before=store->state().encode();rejects([&]{reading.seen("1002",4096);});CHECK(store->state().encode()==before);++guards;
    auto corrupted=saved;corrupted.back()^=2;rejects([&]{reading.restore(corrupted);});CHECK(reading.checkpoint()==saved);++guards;
    std::uint32_t now=1000;bool click=false,skip=false;std::int32_t readable=0;
    s::SceneControls controls([&]{return now;},[&]{return click;},[&]{return click;},[&]{return skip;},[&]{return readable;});
    const auto call=[&](unsigned id,std::initializer_list<int> args){std::vector<s::Value> values;for(auto n:args)values.emplace_back(std::int32_t(n));s::Command command{id,values};return controls.dispatch(command);};
    std::array<s::Value,4> wait_args={s::Value(100),s::Value(0),s::Value(0),s::Value(1)};s::Command wait{236,wait_args};
    CHECK(!controls.dispatch(wait)->complete);click=true;CHECK(!controls.dispatch(wait)->complete);readable=1;CHECK(controls.dispatch(wait)->complete);++cases;
    click=false;readable=0;wait={236,wait_args};CHECK(!controls.dispatch(wait)->complete);const auto pending=controls.checkpoint();
    skip=true;CHECK(controls.dispatch(wait)->complete);skip=false;controls.restore(pending);CHECK(!controls.dispatch(wait)->complete);++cases;
    controls.restore(pending);skip=true;wait_args[2]=s::Value(1);
    for(unsigned n=0;n<98;++n)CHECK(!controls.dispatch(wait)->complete);CHECK(controls.dispatch(wait)->complete);skip=false;++cases;
    wait_args[2]=s::Value(0);wait_args[3]=s::Value(0);click=true;readable=1;wait={236,wait_args};
    CHECK(!controls.dispatch(wait)->complete && !controls.dispatch(wait)->complete);skip=true;CHECK(controls.dispatch(wait)->complete);skip=click=false;++cases;
    call(136,{1,2});CHECK(controls.modes().demo_mode==1 && !controls.automatic_advance() && !controls.automatic_advance() && controls.automatic_advance());++cases;
    call(136,{0,0});controls.auto_mode(true,1);CHECK(!controls.automatic_advance() && controls.automatic_advance());controls.latched_skip(true);call(219,{});CHECK(!controls.modes().auto_mode && !controls.modes().latched_skip);++cases;
    call(221,{1,17});CHECK(controls.modes().adult_mode==1);const auto adult=controls.checkpoint();call(221,{0,0});controls.restore(adult);CHECK(controls.modes().adult_mode==1);++cases;
    call(195,{});now=1250;CHECK(call(204,{})->value->integer()==250);const auto timed=controls.checkpoint();call(195,{});controls.restore(timed);CHECK(call(204,{})->value->integer()==250);++cases;
    auto bad=timed;bad.pop_back();const auto original=controls.checkpoint();rejects([&]{controls.restore(bad);});CHECK(controls.checkpoint()==original);++guards;
    Movie* active{};unsigned recorded{};s::SceneMovie movies([&](auto name,auto flag){CHECK(name=="mv000.pak" && flag==202);auto result=std::make_unique<Movie>();active=result.get();return result;},{},0,[&](auto flag){CHECK(flag==202);++recorded;});
    std::array<s::Value,2> movie_args{s::Value(std::string("mv00")),s::Value(202)};s::Command movie{193,movie_args};
    CHECK(!movies.dispatch(movie)->complete && recorded==0);active->done=true;CHECK(movies.dispatch(movie)->complete && recorded==1 && !movies.playback());++cases;
    movie={193,movie_args};CHECK(!movies.dispatch(movie)->complete);movies.stop();CHECK(recorded==2 && active->stopped);movies.stop();CHECK(recorded==2 && movies.dispatch(movie)->complete);++cases;
    movie_args[1]=s::Value(1024);movie={193,movie_args};rejects([&]{movies.dispatch(movie);});++guards;
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-control-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards
        <<",\"persisted_read_rows\":true,\"four_argument_wait\":true,\"read_gated_interrupt\":true,\"native_rng\":true,\"synthetic_only\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    report.close();CHECK(report);std::cout<<"PASS native script math, reading history, wait policy and control checkpoints.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
