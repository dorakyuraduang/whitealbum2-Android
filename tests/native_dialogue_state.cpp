#include "scene_dialogue.hpp"
#include "checkpoint_codec.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>

namespace s=wa2::script;namespace t=wa2::text;namespace i=wa2::image;namespace r=wa2::resource;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
struct Bytes:r::Source {std::vector<std::uint8_t> data;std::uint64_t size()const override{return data.size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{CHECK(at<=data.size() && out.size()<=data.size()-at);std::copy_n(data.data()+at,out.size(),out.data());}};
void put(std::vector<std::uint8_t>& bytes,std::size_t at,std::uint32_t value){for(unsigned n=0;n<4;++n)bytes.at(at+n)=std::uint8_t(value>>(8*n));}
r::Archive fixture(bool fonts=false){auto source=std::make_shared<Bytes>();auto& b=source->data;const unsigned count=fonts?0:8;b.resize(16+count*44);std::memcpy(b.data(),"KCAP",4);put(b,12,count);
    constexpr std::array<const char*,8> names{"sys_00001.tga","sys_00000.tga","sys_00010.tga","sys_00011.tga","sys_10001.tga","sys_10000.tga","sys_10010.tga","sys_10011.tga"};
    for(unsigned n=0;n<count;++n){const auto at=b.size();std::memcpy(b.data()+20+n*44,names[n],13);put(b,52+n*44,std::uint32_t(at));put(b,56+n*44,21);b.resize(at+21);b[at+2]=2;b[at+12]=b[at+14]=1;b[at+16]=24;b[at+17]=32;}
    return r::Archive::open(source);}
s::CommandResult invoke(s::SceneDialogue& d,unsigned index,std::initializer_list<s::Value> args){std::vector<s::Value> values(args);s::Command call{index,values};return *d.dispatch(call);}
template<class F>void until(F&& f){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!f()){CHECK(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
std::vector<std::uint8_t> patch(std::vector<std::uint8_t> bytes,std::size_t at,std::uint32_t value){put(bytes,at,value);put(bytes,bytes.size()-4,wa2::checkpoint::crc(std::span(bytes).first(bytes.size()-4)));return bytes;}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},layouts{},seen{},loads{};
    for(const auto* raw:{"","ABC","A\\kB","<RAB|cd>","<Aabc>","<C2:<F20:Ab>><W2:Z>"}){
        const auto original=t::layout(raw);const auto bytes=t::checkpoint_layout(original);const auto restored=t::restore_layout(bytes);
        CHECK(t::checkpoint_layout(restored)==bytes && restored.letters.size()==original.letters.size() && restored.pages.size()==original.pages.size());++cases;++layouts;
    }
    auto layout=t::layout("AB");layout.letters[0].x=-0.f;const auto exact=t::checkpoint_layout(layout);CHECK(t::checkpoint_layout(t::restore_layout(exact))==exact);++cases;
    auto bad=layout;bad.letters[0].scale=0;rejects([&]{t::checkpoint_layout(bad);});++guards;
    bad=layout;bad.letters[0].page=99;rejects([&]{t::checkpoint_layout(bad);});++guards;
    bad=layout;bad.letters[0].time=std::numeric_limits<unsigned>::max();rejects([&]{t::checkpoint_layout(bad);});++guards;
    bad=layout;bad.letters[0].y=std::numeric_limits<float>::quiet_NaN();rejects([&]{t::checkpoint_layout(bad);});++guards;
    bad=layout;bad.style.cell=0;rejects([&]{t::checkpoint_layout(bad);});++guards;
    rejects([&]{t::restore_layout(patch(exact,52,4097));});++guards;
    auto corrupt=exact;corrupt.back()^=1;rejects([&]{t::restore_layout(corrupt);});++guards;
    i::ImageStore images(fixture());t::FontBook fonts(fixture(true));const auto loader=[&](auto name){++loads;return images.request(name);};
    auto deferred=fonts.defer(t::layout("ABC"));const auto deferred_layout=t::checkpoint_layout(deferred.metrics());CHECK(deferred.image_bytes()==0 && deferred.metrics().letters.size()==3 && !fonts.source_bytes());++cases;
    rejects([&]{fonts.materialize(deferred);});CHECK(!deferred.image_bytes() && t::checkpoint_layout(deferred.metrics())==deferred_layout);++guards;
    s::SceneDialogue dialogue(fonts,loader,[&](auto id){CHECK(id==7 || id==8);++seen;},[]{}),restored(fonts,loader,{},[]{});
    restored.restore(dialogue.checkpoint());CHECK(dialogue.checkpoint()==restored.checkpoint() && !loads);++cases;
    invoke(dialogue,131,{s::Value(std::string("ABC\\kDEF")),s::Value(7),s::Value(1)});CHECK(seen==1 && dialogue.text().metrics().pages.size()==2 && dialogue.text().metrics().letters.size()==6 && !dialogue.text().image_bytes());++cases;
    until([&]{dialogue.update();return dialogue.ready();});const auto opening=dialogue.checkpoint();restored.restore(opening);CHECK(restored.checkpoint()==opening && seen==1);++cases;
    for(unsigned n=0;n<4;++n){dialogue.update();restored.update();CHECK(dialogue.checkpoint()==restored.checkpoint());}++cases;
    until([&]{dialogue.update();return dialogue.phase()==s::DialoguePhase::waiting;});dialogue.advance();CHECK(dialogue.page()==1 && dialogue.phase()==s::DialoguePhase::typing);++cases;
    dialogue.visible(false);const auto typing=dialogue.checkpoint();restored.restore(typing);CHECK(restored.checkpoint()==typing && restored.page()==1 && seen==1);dialogue.update();restored.update();CHECK(restored.checkpoint()==dialogue.checkpoint());++cases;
    s::DialogueSettings skin;skin.skin=1;skin.novel_alpha=127;dialogue.settings(skin);const auto before=loads;until([&]{dialogue.update();return dialogue.ready();});CHECK(loads==before+4);++cases;
    invoke(dialogue,220,{s::Value(1)});invoke(dialogue,233,{s::Value(5),s::Value(0)});dialogue.update();CHECK(dialogue.novel().opacity()==102 && !dialogue.novel().text_visible());
    restored.restore(dialogue.checkpoint());for(unsigned n=0;n<5;++n){dialogue.update();restored.update();CHECK(dialogue.checkpoint()==restored.checkpoint());}++cases;
    invoke(dialogue,143,{s::Value(3),s::Value(0)});restored.restore(dialogue.checkpoint());for(unsigned n=0;n<4;++n){dialogue.update();restored.update();CHECK(dialogue.checkpoint()==restored.checkpoint());}
    CHECK(dialogue.phase()==s::DialoguePhase::closed && dialogue.raw_text().empty() && !fonts.source_bytes());++cases;
    const auto stable=dialogue.checkpoint();auto broken=stable;broken.back()^=4;rejects([&]{dialogue.restore(broken);});CHECK(dialogue.checkpoint()==stable);++guards;
    const auto idle_loads=loads;restored.restore(stable);restored.update();CHECK(restored.checkpoint()==stable && loads==idle_loads);++cases;
    for(const auto [at,value]:std::array<std::pair<std::size_t,unsigned>,4>{{{12,10},{stable.size()-60,6},{stable.size()-36,4096},{stable.size()-24,2}}}){
        rejects([&]{dialogue.restore(patch(stable,at,value));});CHECK(dialogue.checkpoint()==stable);++guards;
    }
    CHECK(seen==1 && !fonts.source_bytes());++cases;
    {s::SceneDialogue visible(fonts,loader,{},[]{});invoke(visible,131,{s::Value(std::string("ABC")),s::Value(8),s::Value(1)});until([&]{visible.update();return visible.ready();});visible.update(true);
        const auto state=visible.checkpoint();rejects([&]{visible.sprites();});CHECK(visible.checkpoint()==state && !visible.text().image_bytes() && !fonts.source_bytes());++guards;++cases;}
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-dialogue-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"metadata_text_layouts\":"<<layouts<<",\"glyph_pixels_decoded\":false,\"synthetic_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS native dialogue/layout checkpoints and skin reload: "<<cases<<" cases.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
