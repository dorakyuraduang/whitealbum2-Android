#include "audio_binding.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace a=wa2::audio;namespace p=wa2::platform;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
struct State {std::weak_ptr<a::Mixer> owner;bool opened{},paused{},bad_owner{},fail_open{},throw_close{};unsigned opens{},closes{},pauses{},destroyed{};};
struct Output:p::AudioOutput {
    std::shared_ptr<State> state;std::vector<std::string>& events;unsigned id;int& active;int& peak;
    Output(std::shared_ptr<State> value,std::vector<std::string>& log,unsigned number,int& count,int& high):state(std::move(value)),events(log),id(number),active(count),peak(high){}
    ~Output(){state->bad_owner|=state->owner.expired();++state->destroyed;if(state->opened)--active;}
    bool open(const std::string&)override{state->bad_owner|=state->owner.expired();++state->opens;events.push_back("open"+std::to_string(id));if(state->fail_open)return false;state->opened=true;peak=std::max(peak,++active);return true;}
    void close()override{state->bad_owner|=state->owner.expired();++state->closes;events.push_back("close"+std::to_string(id));if(state->opened){state->opened=false;--active;}if(state->throw_close)throw std::runtime_error("injected close failure");}
    void pause(bool value)override{state->bad_owner|=state->owner.expired();state->paused=value;++state->pauses;}
    std::vector<std::string> devices()override{return {};}
    p::AudioStatus status()const override{p::AudioStatus value;value.error="injected open failure";return value;}
};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};rejects([]{p::AudioBinding invalid({});});++guards;++cases;
    std::vector<std::shared_ptr<State>> outputs;std::vector<std::string> events;std::weak_ptr<a::Mixer> expected;bool null_factory{},throw_factory{},fail_open{},throw_close{};int active{},peak{};
    p::AudioBinding binding([&](a::Mixer& mixer)->std::unique_ptr<p::AudioOutput>{CHECK(expected.lock().get()==&mixer);if(throw_factory)throw std::runtime_error("injected factory failure");if(null_factory)return {};auto state=std::make_shared<State>();state->owner=expected;state->fail_open=fail_open;state->throw_close=throw_close;outputs.push_back(state);return std::make_unique<Output>(state,events,unsigned(outputs.size()),active,peak);});
    binding.synchronize({},false);CHECK(outputs.empty() && !binding.mixer());++cases;
    auto first=std::make_shared<a::Mixer>();expected=first;binding.synchronize(first,false);CHECK(outputs.size()==1 && active==1 && outputs[0]->opens==1 && outputs[0]->pauses==1 && !outputs[0]->paused && binding.mixer()==first);++cases;
    for(unsigned n=0;n<16;++n)binding.synchronize(first,false);CHECK(outputs.size()==1 && outputs[0]->opens==1 && outputs[0]->pauses==1 && !outputs[0]->closes);++cases;
    binding.synchronize(first,true);binding.synchronize(first,true);CHECK(outputs[0]->paused && outputs[0]->pauses==2 && active==1);binding.synchronize(first,false);CHECK(!outputs[0]->paused && outputs[0]->pauses==3);++cases;
    auto old=std::weak_ptr(first);first.reset();CHECK(!old.expired());auto second=std::make_shared<a::Mixer>();expected=second;binding.synchronize(second,true);
    CHECK(old.expired() && active==1 && peak==1 && events==std::vector<std::string>({"open1","close1","open2"}) && outputs[0]->destroyed==1 && !outputs[0]->bad_owner && outputs[1]->paused);++cases;
    auto next=std::make_shared<a::Mixer>();expected=next;null_factory=true;rejects([&]{binding.synchronize(next,false);});null_factory=false;CHECK(binding.mixer()==second && active==1 && outputs.size()==2 && !outputs[1]->closes);++guards;++cases;
    throw_factory=true;rejects([&]{binding.synchronize(next,false);});throw_factory=false;CHECK(binding.mixer()==second && active==1 && !outputs[1]->closes);++guards;++cases;
    auto old_second=std::weak_ptr(second);second.reset();fail_open=true;rejects([&]{binding.synchronize(next,false);});fail_open=false;CHECK(old_second.expired() && !binding.mixer() && active==0 && outputs[1]->destroyed==1 && outputs[2]->destroyed==1 && outputs[2]->closes==1 && !outputs[1]->bad_owner && !outputs[2]->bad_owner);++guards;++cases;
    binding.synchronize(next,false);CHECK(binding.mixer()==next && outputs.size()==4 && active==1 && peak==1);++cases;
    next.reset();binding.reset();CHECK(!binding.mixer() && active==0 && outputs[3]->destroyed==1 && !outputs[3]->bad_owner);binding.reset();CHECK(outputs[3]->closes==1);++cases;
    auto last=std::make_shared<a::Mixer>();expected=last;throw_close=true;binding.synchronize(last,false);last.reset();rejects([&]{binding.reset();});throw_close=false;CHECK(!binding.mixer() && active==0 && outputs[4]->destroyed==1 && !outputs[4]->bad_owner);++guards;++cases;
    {auto mixer=std::make_shared<a::Mixer>();expected=mixer;p::AudioBinding scoped([&](auto&)->std::unique_ptr<p::AudioOutput>{auto state=std::make_shared<State>();state->owner=expected;state->throw_close=true;outputs.push_back(state);return std::make_unique<Output>(state,events,unsigned(outputs.size()),active,peak);});scoped.synchronize(mixer,false);mixer.reset();}CHECK(active==0 && outputs.back()->destroyed==1 && !outputs.back()->bad_owner);++cases;
    CHECK(std::none_of(outputs.begin(),outputs.end(),[](auto state){return state->bad_owner;}));++cases;
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-audio-binding.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"peak_live_outputs\":"<<peak<<",\"mixer_alive_until_detach\":true,\"unchanged_owner_keeps_output\":true,\"fake_outputs_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(report);std::cout<<"PASS native mixer/output ownership, ordered handoff and failure cleanup.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
