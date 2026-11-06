#include "saved_scene.hpp"
#include "checkpoint_codec.hpp"
#include <fstream>
#include <iostream>
#include <limits>

namespace g=wa2::game;namespace c=wa2::checkpoint;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
using Bytes=std::vector<std::uint8_t>;
void put(Bytes& b,unsigned at,unsigned value){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(value>>(8*n));}
Bytes patch(Bytes b,unsigned at,unsigned value){put(b,at,value);put(b,unsigned(b.size()-4),c::crc(std::span(b).first(b.size()-4)));return b;}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};g::SavedScene value;value.logical_us=123456789;value.random=0xffffffff;value.command=238;value.ticks=std::numeric_limits<std::uint64_t>::max();value.messages=90;value.voices=17;value.scene="synthetic_scene";value.background_changing=true;value.preserve_characters=true;value.window_visible=false;value.skip_read_only=false;value.replay=true;
    for(unsigned n=0;n<value.parts.size();++n){c::Writer out;out.u32(n);value.parts[n]=out.seal(g::scene_part_tags[n]);}
    const auto saved=value.encode();const auto restored=g::SavedScene::decode(saved);CHECK(restored.encode()==saved && restored.scene==value.scene && restored.random==value.random && restored.logical_us==value.logical_us && restored.ticks==value.ticks);++cases;
    for(unsigned n=0;n<restored.parts.size();++n){auto in=c::Reader::open(g::scene_part_tags[n],restored.parts[n]);CHECK(in.u32()==n);in.finish();++cases;}
    for(auto clock:{std::int64_t(0),std::numeric_limits<std::int64_t>::max()}){auto changed=value;changed.logical_us=clock;CHECK(g::SavedScene::decode(changed.encode()).logical_us==clock);++cases;}
    auto corrupt=saved;corrupt.back()^=1;rejects([&]{g::SavedScene::decode(corrupt);});++guards;
    for(unsigned size:{0u,15u,unsigned(saved.size()-1)}){rejects([&]{g::SavedScene::decode(std::span(saved).first(size));});++guards;}
    const auto name=unsigned(value.scene.size());for(auto [at,data]:std::array<std::pair<unsigned,unsigned>,5>{{{16,0x80000000},{24,65536},{52,241},{56+name,2},{76+name,18}}}){rejects([&]{g::SavedScene::decode(patch(saved,at,data));});++guards;}
    auto inner=saved;inner.at(84+name+12)^=1;put(inner,unsigned(inner.size()-4),c::crc(std::span(inner).first(inner.size()-4)));rejects([&]{g::SavedScene::decode(inner);});++guards;
    for(const auto& id:{std::string(),std::string("../scene"),std::string("a/b"),std::string("a\\b"),std::string("a\0b",3),std::string(241,'x')}){auto changed=value;changed.scene=id;rejects([&]{changed.encode();});++guards;}
    for(unsigned n=0;n<value.parts.size();++n){auto changed=value;changed.parts[n].clear();rejects([&]{changed.encode();});++guards;}
    auto swapped=value;std::swap(swapped.parts[0],swapped.parts[1]);rejects([&]{swapped.encode();});++guards;
    auto negative=value;negative.logical_us=-1;rejects([&]{negative.encode();});++guards;CHECK(value.encode()==saved);
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-saved-scene.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"subsystem_envelopes\":17,\"envelope_checks_only\":true,\"scene_instances_created\":0,\"original_resources_opened\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS native aggregate save envelopes and metadata, without creating a game scene.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
