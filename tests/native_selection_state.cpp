#include "scene_selection.hpp"
#include "resource_catalog.hpp"
#include "checkpoint_codec.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

namespace s=wa2::script;namespace r=wa2::resource;namespace p=wa2::progress;namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
struct Bytes:r::Source {
    std::vector<std::uint8_t> bytes;
    std::uint64_t size()const override{return bytes.size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{if(at>bytes.size() || out.size()>bytes.size()-at)throw r::Error("Selection fixture bounds");if(!out.empty())std::memcpy(out.data(),bytes.data()+at,out.size());}
};
struct Storage:p::Storage {std::optional<p::Bytes> load()override{return {};}void write(p::Block,std::span<const std::uint8_t>)override{}};
void put(std::vector<std::uint8_t>& b,std::size_t at,std::uint32_t v){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(v>>(8*n));}
void half(std::vector<std::uint8_t>& b,std::size_t at,unsigned v){b.at(at)=std::uint8_t(v);b.at(at+1)=std::uint8_t(v>>8);}
std::shared_ptr<Bytes> fixture(){auto result=std::make_shared<Bytes>();auto& b=result->bytes;b.resize(104);std::memcpy(b.data(),"KCAP",4);put(b,12,2);
    for(unsigned n=0;n<2;++n){const auto width=n?784u:248u,height=n?216u:104u;const auto start=b.size();const auto size=18+width*height*4;
        std::memcpy(b.data()+20+n*44,n?"sys_00200.tga":"sys_00011.tga",13);put(b,52+n*44,std::uint32_t(start));put(b,56+n*44,size);b.resize(start+size);
        b[start+2]=2;half(b,start+12,width);half(b,start+14,height);b[start+16]=32;b[start+17]=40;std::fill(b.begin()+start+18,b.end(),255);}
    return result;}
s::CommandResult invoke(s::SceneChoices& choices,unsigned id,std::initializer_list<s::Value> args){std::vector<s::Value> values(args);s::Command call{id,values};return *choices.dispatch(call);}
void entry(s::SceneChoices& choices,const char* text,int flag=0,int expected=0,bool disabled=false){invoke(choices,208,{s::Value(std::string(text)),s::Value(flag),s::Value(expected),s::Value(int(disabled))});}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},prepared{};auto source=fixture();r::Catalog resources([&](auto name)->std::shared_ptr<r::Source>{return name=="grp.pak"?source:nullptr;});auto progress=std::make_shared<p::Store>(std::make_shared<Storage>());progress->state().set_flag(992,1);
    std::array<unsigned,3> sounds{};s::SceneChoices choices(progress,[]{return "1001";});s::SceneSelection view([&](auto name){return resources.image("grp",name);},[&](auto raw){CHECK(!raw.empty());++prepared;return wa2::text::PreparedText{};},[&](auto sound){++sounds[unsigned(sound)];});
    entry(choices,"first");entry(choices,"hidden",1,1);entry(choices,"disabled",2,1,true);entry(choices,"last");invoke(choices,209,{s::Value(0)});view.sync(choices);
    CHECK(view.rows().size()==3 && view.rows()[0].ordinal==0 && view.rows()[1].ordinal==2 && view.rows()[2].ordinal==3 && view.rows()[0].chosen && !view.ready());++cases;
    CHECK(view.rows()[0].y==208 && view.rows()[1].y==292 && view.rows()[2].y==376 && !view.rows()[1].enabled);++cases;
    CHECK(!view.input({true,true,false,400,220,0}));++cases;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);do{view.update();if(!view.loading())break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<deadline);
    CHECK(!view.loading() && view.opacity()==8 && !view.ready());++cases;
    for(unsigned tick=2;tick<=30;++tick){view.update();CHECK(view.opacity()==(tick==30?255:tick*255/30) && view.ready()==(tick==30));}++cases;
    const auto count=prepared;view.sync(choices);view.sync(choices);CHECK(prepared==count);++cases;
    auto sprites=view.sprites();CHECK(sprites.size()==7 && sprites[0].x==248 && sprites[0].y==194 && sprites[0].source_y==144 && sprites[0].blend==i::SpriteBlend::add && sprites[1].source_y==0 && sprites.back().x==328 && sprites.back().source_x==208 && sprites.back().layer==0x1058);++cases;
    CHECK(sprites[0].order==0x454 && sprites[1].order==0x455 && sprites[2].order==0x45c && sprites.back().order==0x457);++cases;
    CHECK(!view.input({true,false,false,400,300,0}) && view.focused()==2 && sounds[0]==0);CHECK(!view.input({true,false,false,248,220,0}) && !view.focused());++cases;
    CHECK(view.input({true,true,false,400,390,0})==3 && view.focused()==3);++cases;
    sprites=view.sprites();CHECK(sprites.size()==8);++cases;
    CHECK(!view.input({false,false,false,0,0,1}) && view.focused()==0);CHECK(!view.input({false,false,false,0,0,1}) && view.focused()==2);CHECK(!view.input({false,false,false,0,0,1}) && view.focused()==3);++cases;
    CHECK(!view.input({false,false,false,0,0,-1}) && view.focused()==2 && !view.input({false,false,true,0,0,0}) && sounds[2]==1);
    CHECK(!view.input({false,false,false,0,0,-1}) && view.focused()==0 && view.input({false,false,true,0,0,0})==0);++cases;
    view.replay_mode(true);CHECK(view.input({true,true,false,400,220,0})==0);CHECK(!view.input({true,true,false,400,390,0}) && sounds[2]==2 && choices.active());view.replay_mode(false);++cases;
    const auto before=view.rows().size();rejects([&]{view.input({true,false,false,std::numeric_limits<float>::quiet_NaN(),0,0});});rejects([&]{view.input({false,false,false,0,0,2});});CHECK(view.rows().size()==before);guards+=2;
    s::SelectionSettings bad;bad.skin=10;rejects([&]{view.settings(bad);});++guards;
    rejects([&]{s::SceneSelection missing({},[](auto){return wa2::text::PreparedText{};});});++guards;
    choices.choose(*view.input({true,true,false,400,390,0}));CHECK((progress->state().flag(992)&9)==9);view.sync(choices);CHECK(view.rows().empty() && view.sprites().empty() && !view.ready());++cases;
    for(unsigned n:{1u,2u,8u}){s::SceneChoices separate(progress,[]{return "1001";});for(unsigned row=0;row<n;++row)entry(separate,"fixture");invoke(separate,209,{s::Value(0)});view.sync(separate);
        CHECK(view.rows().size()==n && view.rows().front().y==334-int(n)*42 && view.rows().back().y==334-int(n)*42+int(n-1)*84);
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);do{view.update([](auto){return 0;});if(!view.loading())break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<until);
        CHECK(view.ready() && view.opacity()==255);CHECK(view.input({false,false,false,0,0,-1})==std::nullopt && view.focused()==0);++cases;
    }
    rejects([&]{choices.previously_chosen(8);});++guards;
    unsigned loads{},restore_sounds{};const auto loader=[&](auto name){++loads;return resources.image("grp",name);};const auto text=[&](auto raw){CHECK(!raw.empty());return wa2::text::PreparedText{};};
    const i::Resolve resolve=[&](const auto& ref){auto request=resources.image(ref);const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(std::chrono::steady_clock::now()<end){if(auto image=request->get())return image;std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("Saved selection image wait expired");};
    s::SceneChoices saved_choices(progress,[]{return "1001";});s::SceneSelection saved_view(loader,text),restored_view(loader,text,[&](auto){++restore_sounds;});
    const auto empty_view=saved_view.checkpoint(saved_choices,resources);restored_view.restore(empty_view,saved_choices,resources,resolve);CHECK(restored_view.checkpoint(saved_choices,resources)==empty_view && !loads);++cases;
    entry(saved_choices,"first");entry(saved_choices,"disabled",100,1,true);entry(saved_choices,"last");invoke(saved_choices,209,{s::Value(0)});saved_view.sync(saved_choices);const auto pending_view=saved_view.checkpoint(saved_choices,resources);const auto load_count=loads;restored_view.restore(pending_view,saved_choices,resources,resolve);CHECK(restored_view.checkpoint(saved_choices,resources)==pending_view && restored_view.loading() && loads==load_count && !restore_sounds);++cases;
    resolve(resources.reference(resources.image("grp","sys_00200.tga")));resolve(resources.reference(resources.image("grp","sys_00011.tga")));for(unsigned n=0;n<8;++n)saved_view.update();const auto midway=saved_view.checkpoint(saved_choices,resources);restored_view.restore(midway,saved_choices,resources,resolve);
    for(unsigned n=0;n<22;++n){saved_view.update();restored_view.update();CHECK(saved_view.checkpoint(saved_choices,resources)==restored_view.checkpoint(saved_choices,resources) && saved_view.sprites().size()==restored_view.sprites().size());}CHECK(saved_view.ready() && restored_view.ready());++cases;
    saved_view.input({true,false,false,400,saved_view.rows().back().y+4,0});const auto focused=saved_view.checkpoint(saved_choices,resources);restored_view.restore(focused,saved_choices,resources,resolve);CHECK(restored_view.focused()==2 && !restore_sounds && restored_view.input({false,false,true,0,0,0})==2);++cases;
    s::SceneChoices clone(progress,[]{return "1001";});clone.restore(saved_choices.checkpoint());restored_view.restore(focused,clone,resources,resolve);CHECK(restored_view.checkpoint(clone,resources)==focused);++cases;
    auto corrupt=focused;corrupt.back()^=1;rejects([&]{restored_view.restore(corrupt,clone,resources,resolve);});CHECK(restored_view.checkpoint(clone,resources)==focused);++guards;
    const auto signature_size=unsigned(saved_choices.checkpoint().size());
    for(auto [at,value]:std::array<std::pair<unsigned,unsigned>,5>{{{16+signature_size,10},{20+signature_size,2},{24+signature_size,256},{76+signature_size,256},{84+signature_size,9}}}){auto broken=focused;put(broken,at,value);put(broken,broken.size()-4,wa2::checkpoint::crc(std::span(broken).first(broken.size()-4)));rejects([&]{restored_view.restore(broken,clone,resources,resolve);});CHECK(restored_view.checkpoint(clone,resources)==focused);++guards;}
    s::SceneChoices wrong(progress,[]{return "1001";});entry(wrong,"other");invoke(wrong,209,{s::Value(0)});rejects([&]{restored_view.restore(focused,wrong,resources,resolve);});++guards;
    rejects([&]{restored_view.restore(focused,clone,resources,{});});++guards;rejects([&]{restored_view.restore(focused,clone,resources,[](const auto&)->std::shared_ptr<const i::Raster>{return {};});});++guards;
    progress->state().set_flag(992,0);s::SceneChoices history_choices(progress,[]{return "1001";});entry(history_choices,"fixture");invoke(history_choices,209,{s::Value(0)});s::SceneSelection history_view(loader,text);history_view.sync(history_choices);history_view.update([](auto){return 0;});CHECK(history_view.ready());const auto without_history=history_view.checkpoint(history_choices,resources);progress->state().set_flag(992,1);const auto history_loads=loads;restored_view.restore(without_history,history_choices,resources,resolve);CHECK(loads==history_loads+1 && restored_view.rows()[0].chosen && !restored_view.ready());restored_view.update([](auto){return 0;});CHECK(restored_view.ready() && restored_view.sprites().size()==3);++cases;
    saved_choices.choose(2);rejects([&]{saved_view.checkpoint(saved_choices,resources);});++guards;saved_view.sync(saved_choices);const auto inactive=saved_view.checkpoint(saved_choices,resources);restored_view.restore(inactive,saved_choices,resources,resolve);CHECK(restored_view.sprites().empty() && restored_view.checkpoint(saved_choices,resources)==inactive);++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-selection-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"synthetic_only\":true,\"font_preparation_stubbed\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";out.close();CHECK(out);
    std::cout<<"PASS native selection layout, focus, fade and original ordinals.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
