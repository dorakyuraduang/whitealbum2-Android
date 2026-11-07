#include "options_view.hpp"
#include "scene_geometry.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>

namespace g=wa2::game;namespace r=wa2::resource;namespace i=wa2::image;namespace a=wa2::audio;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
void put(std::vector<std::uint8_t>& b,std::size_t at,std::uint32_t v){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(v>>(8*n));}
struct Bytes:r::Source {
    std::vector<std::uint8_t> b;std::uint64_t size()const override{return b.size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{CHECK(at<=b.size() && out.size()<=b.size()-at);std::copy_n(b.data()+at,out.size(),out.data());}
};
std::shared_ptr<Bytes> fixture(bool small=false){
    struct Atlas {const char* name;unsigned w,h;};
    const Atlas atlases[]{
        {"sys_03000.tga",1280,36},{"sys_09000.tga",1280,56},{"sys_03010.tga",756,96},{"sys_03011.tga",576,96},
        {"sys_03020.tga",432,128},{"sys_03030.tga",208,72},{"sys_09100.tga",56,72},
        {"sys_03100.tga",1024,464},{"sys_03200.tga",1024,464},{"sys_03201.tga",1024,464},{"sys_03040.tga",944,216},
        {"sys_03101.tga",1024,464},{"sys_03210.tga",1024,464},{"sys_03211.tga",1024,464},{"sys_03041.tga",944,120},
        {"sys_03102.tga",1024,464},{"sys_03220.tga",1024,464},{"sys_03221.tga",1024,464},{"sys_03042.tga",944,24}};
    auto source=std::make_shared<Bytes>();auto& b=source->b;b.resize(16+44*std::size(atlases));std::memcpy(b.data(),"KCAP",4);put(b,12,unsigned(std::size(atlases)));
    for(unsigned n=0;n<std::size(atlases);++n){const auto& atlas=atlases[n];const auto w=small?1:atlas.w,h=small?1:atlas.h;const auto at=b.size();const auto size=18+w*h*3;
        std::memcpy(b.data()+20+n*44,atlas.name,std::strlen(atlas.name));put(b,52+n*44,unsigned(at));put(b,56+n*44,size);b.resize(at+size,127);
        std::fill_n(b.data()+at,18,0);b[at+2]=2;b[at+12]=std::uint8_t(w);b[at+13]=std::uint8_t(w>>8);b[at+14]=std::uint8_t(h);b[at+15]=std::uint8_t(h>>8);b[at+16]=24;b[at+17]=32;}
    return source;
}
g::OptionsInput pointer(float x,float y,bool click=false,bool held=false){return {{true,click,false,x,y,0},false,held};}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},writes{},max_pending{},max_requests_per_tick{};bool fail_write{},missing{};
    g::OptionsState saved;std::vector<a::SystemCue> sounds;std::vector<std::string> names;
    std::vector<std::weak_ptr<i::ImageRequest>> requests;std::mutex mutex;std::condition_variable wake;bool worker_released{};
    auto source=fixture();i::ImageStore images(r::Archive::open(source),[&]{std::unique_lock lock(mutex);CHECK(wake.wait_for(lock,std::chrono::seconds(10),[&]{return worker_released;}));});
    const g::OptionsView::Loader loader=[&](std::string_view name){if(missing)return std::shared_ptr<i::ImageRequest>{};unsigned pending{};for(auto& weak:requests)if(auto request=weak.lock())pending+=!request->get();
        CHECK(pending==0);names.emplace_back(name);auto request=images.request(name);requests.push_back(request);max_pending=std::max(max_pending,pending+unsigned(!request->get()));return request;};
    g::OptionsView view(loader,[&](const auto& state){if(fail_write)throw std::runtime_error("owned settings write failed");saved=state;++writes;},[&](auto sound){sounds.push_back(sound);});
    const auto step=[&](g::OptionsInput in={}){const auto before=names.size();view.update(in);max_requests_per_tick=std::max(max_requests_per_tick,unsigned(names.size()-before));};
    const auto ready=[&]{const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);unsigned ticks{};while(view.phase()!=g::OptionsPhase::active){CHECK(++ticks<2000 && std::chrono::steady_clock::now()<deadline);step();std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    const auto click=[&](unsigned id,float offset=1.f,bool held=false){const auto it=std::find_if(view.hits().begin(),view.hits().end(),[&](const auto& hit){return hit.id==id;});CHECK(it!=view.hits().end());const auto hit=*it;step(pointer(hit.x+offset,hit.y+1,true,held));};
    const auto sprite=[&](int order)->const i::Sprite&{const auto it=std::find_if(view.sprites().begin(),view.sprites().end(),[&](const auto& item){return item.order==order;});CHECK(it!=view.sprites().end());return *it;};
    const auto geometry=[&]{for(unsigned n=0;n<view.sprites().size();++n){const auto& item=view.sprites()[n];CHECK(item.image);CHECK(i::scene_quad({item.x,item.y,item.width,item.height,item.source_x,item.source_y,item.source_width,item.source_height},item.image->width,item.image->height));if(n)CHECK(view.sprites()[n-1].layer<=item.layer);}};
    rejects([&]{step();});++guards;CHECK(view.phase()==g::OptionsPhase::idle && view.sprites().empty());++cases;
    view.prepare(saved);CHECK(names.size()==1 && names[0]=="sys_03000.tga" && view.phase()==g::OptionsPhase::loading);for(unsigned n=0;n<20;++n)step();CHECK(names.size()==1 && view.sprites().empty() && writes==0);++cases;
    {std::lock_guard lock(mutex);worker_released=true;}wake.notify_all();ready();CHECK(names.size()==11 && max_pending<=1 && max_requests_per_tick<=2 && view.page()==0 && view.hits().size()==34 && view.state()==saved);geometry();++cases;
    CHECK(sprite(0x4b5).x==128 && sprite(0x4b5).y==112 && sprite(0x4b5).width==1024 && sprite(0x4b5).height==464);CHECK(sprite(0x4a8).alpha==128 && sprite(0x4a8).source_width==1);CHECK(sprite(0x4ad).x==1164 && sprite(0x4ad).y==688);++cases;
    const auto top=i::scene_quad({0,0,1280,40,0,0,1280,40},1280,36);CHECK(top);++cases;
    step(pointer(583.99f,124));CHECK(!view.focused());step(pointer(584,124));CHECK(view.focused()==10 && sounds.back()==a::SystemCue::focus);step(pointer(840,124));CHECK(view.focused()==11);step(pointer(1096,124));CHECK(!view.focused());++cases;
    click(11);CHECK(!writes);click(10);CHECK(writes==1 && saved.value(g::Option::display)==0 && view.state()==saved);++cases;
    const auto before=view.state();fail_write=true;rejects([&]{click(11);});++guards;fail_write=false;CHECK(view.state()==before && saved==before && writes==1);++cases;
    click(42);CHECK(saved.value(g::Option::auto_delay)==60);click(42,8);CHECK(saved.value(g::Option::auto_delay)==85);click(42,335);CHECK(saved.value(g::Option::auto_delay)==600);click(40);CHECK(saved.value(g::Option::auto_delay)==574);click(41);CHECK(saved.value(g::Option::auto_delay)==600);++cases;
    click(62,335,true);CHECK(view.state().value(g::Option::window_opacity)==256);step(pointer(-1000,-1000,false,true));CHECK(view.focused()==62 && view.state().value(g::Option::window_opacity)==0);step(pointer(2000,2000,false,true));CHECK(view.state().value(g::Option::window_opacity)==256);step(pointer(2000,2000));CHECK(!view.focused());++cases;
    click(60);CHECK(view.state().value(g::Option::window_opacity)==243);click(61);CHECK(view.state().value(g::Option::window_opacity)==256);geometry();++cases;
    click(20);CHECK(saved.value(g::Option::text_speed)==0);click(23);CHECK(saved.value(g::Option::text_speed)==3);click(100);CHECK(saved.value(g::Option::window_visibility)==0);click(101);CHECK(saved.value(g::Option::window_visibility)==1);++cases;
    const auto old_atlas=sprite(0x4b5).image;click(1);CHECK(view.phase()==g::OptionsPhase::page_loading && view.page()==0 && sprite(0x4b5).image==old_atlas);ready();CHECK(view.page()==1 && view.hits().size()==38 && names.size()==15 && sprite(0x4b5).image!=old_atlas);geometry();++cases;
    click(33);CHECK(saved.muted[0]);click(34);CHECK(!saved.muted[0]);click(44);CHECK(!saved.muted[1]);click(43);CHECK(saved.muted[1]);click(32,335);CHECK(saved.value(g::Option::master_volume)==256);++cases;
    click(55);auto command=view.take_command();CHECK(command && command->kind==g::OptionsCommand::Kind::preview_effects && !view.take_command());click(65);command=view.take_command();CHECK(command && command->kind==g::OptionsCommand::Kind::preview_voice);++cases;
    step(pointer(673,413));CHECK(view.focused()==70);CHECK(sprite(0x4ac).source_y==72 && sprite(0x4ac).source_width==994);++cases;
    click(3);command=view.take_command();CHECK(command && command->kind==g::OptionsCommand::Kind::reset && view.phase()==g::OptionsPhase::active);const auto writes_before=writes;view.state(g::OptionsState{});CHECK(writes_before==writes && view.state()==g::OptionsState{});++cases;
    missing=true;rejects([&]{click(2);});++guards;missing=false;CHECK(view.phase()==g::OptionsPhase::active && view.page()==1);++cases;
    click(2);ready();CHECK(view.page()==2 && view.hits().size()==27 && names.size()==19);for(unsigned row=0;row<10;++row){click(11+row*10);CHECK(view.state().value(g::Option(unsigned(g::Option::character_0)+row))==1);}geometry();++cases;
    click(8);ready();CHECK(view.page()==0);click(7);ready();CHECK(view.page()==2);g::OptionsInput in;in.page_delta=-1;step(in);ready();CHECK(view.page()==1);++cases;
    in={};in.selection.navigation=1;step(in);CHECK(view.focused()==0);in.selection.navigation=-1;step(in);CHECK(view.focused()==81);++cases;
    in={};in.cancel=true;step(in);CHECK(view.phase()==g::OptionsPhase::closing && !view.take_command());for(unsigned n=0;n<14;++n)step();CHECK(!view.take_command());step();command=view.take_command();CHECK(view.phase()==g::OptionsPhase::closed && command && command->kind==g::OptionsCommand::Kind::close && view.sprites().empty());++cases;
    view.prepare(saved,0,{0,0,0});ready();click(9);step();CHECK(view.phase()==g::OptionsPhase::closed && view.take_command());++cases;
    for(unsigned n=0;n<unsigned(g::Option::count);++n){auto invalid=saved;invalid.values[n]=-1;rejects([&]{view.state(invalid);});++guards;invalid.values[n]=1000;rejects([&]{view.state(invalid);});++guards;}
    rejects([&]{saved.value(g::Option::count);});++guards;rejects([&]{saved.value(g::Option(900),0);});++guards;rejects([&]{view.prepare(saved,3);});++guards;rejects([&]{view.prepare(saved,0,{601,1,1});});++guards;
    in={};in.selection.x=std::numeric_limits<float>::quiet_NaN();rejects([&]{step(in);});++guards;in={};in.page_delta=std::numeric_limits<std::int32_t>::min();rejects([&]{step(in);});++guards;in={};in.selection.navigation=2;rejects([&]{step(in);});++guards;
    view.clear();CHECK(view.phase()==g::OptionsPhase::idle && view.sprites().empty() && view.hits().empty() && !view.take_command());++cases;
    auto small=fixture(true);i::ImageStore bad_images(r::Archive::open(small));g::OptionsView bad([&](auto name){return bad_images.request(name);});bad.prepare(saved);bool rejected{};for(unsigned n=0;n<100 && !rejected;++n){try{bad.update();}catch(const std::exception&){rejected=true;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}CHECK(rejected && bad.sprites().empty());++guards;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-options-view.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"maximum_pending_images\":"<<max_pending<<",\"maximum_new_requests_per_tick\":"<<max_requests_per_tick<<",\"synthetic_images_only\":true,\"preference_storage_bound\":false,\"application_view_bound\":false,\"original_resources_opened\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS common native options geometry, values, transactional edits and bounded loading.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
