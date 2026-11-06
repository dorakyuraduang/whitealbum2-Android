#include "script_session.hpp"
#include "scene_choices.hpp"
#include "checkpoint_codec.hpp"
#include <algorithm>
#include <bit>
#include <fstream>
#include <iostream>
#include <limits>

namespace s=wa2::script;namespace p=wa2::progress;namespace c=wa2::checkpoint;
using Bytes=c::Bytes;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
std::span<const std::uint8_t> text(std::string_view t){return {reinterpret_cast<const std::uint8_t*>(t.data()),t.size()};}
void word(Bytes& b,std::uint32_t v){for(unsigned n=0;n<4;++n)b.push_back(std::uint8_t(v>>(n*8)));}
void put(Bytes& b,std::size_t at,std::uint32_t v){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(v>>(n*8));}
Bytes function(unsigned id,std::initializer_list<std::uint8_t> params){Bytes b;word(b,id);b.resize(124);b[4]='f';b[36]=std::uint8_t(params.size());put(b,40,3);std::copy(params.begin(),params.end(),b.begin()+44);for(std::size_t n=0;n<params.size();++n)b.push_back(',');return b;}
struct Script {
    Bytes code;
    explicit Script(){code={'L','S','C','R'};word(code,20);word(code,1);word(code,0);word(code,20);}
    void op(std::initializer_list<std::uint32_t> values){for(auto v:values)word(code,v);}
    void integer(std::int32_t n){op({5,3,std::bit_cast<std::uint32_t>(n)});}
    std::shared_ptr<const s::Program> finish(std::string name,std::string_view strings={}){op({0,1});return std::make_shared<s::Program>(s::Program::parse(std::move(name),std::move(code),text(strings)));}
};
struct Storage final:p::Storage {
    std::optional<Bytes> data;bool failing{};unsigned writes{};
    std::optional<Bytes> load()override{return data;}
    void write(p::Block block,std::span<const std::uint8_t> b)override{if(failing)throw p::Error("injected storage error");if(!data)data=Bytes(p::layout::system_save_bytes);std::copy(b.begin(),b.end(),data->begin()+p::offset(block));++writes;}
};
Bytes mutated(std::string_view tag,const Bytes& sealed,std::size_t offset,std::uint32_t v){Bytes data(sealed.begin()+12,sealed.end()-4);put(data,offset,v);c::Writer writer;writer.raw(data);return writer.seal(tag);}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},resumed_calls{};
    auto globals=s::Globals::parse(text("1,b,9\r\n2,s,-12\r\n3,i,12345\r\n4,f,0.0\r\n5,t,x\r\n"));
    globals.set(3,s::Value(std::bit_cast<float>(0x80000000u)));globals.set(4,s::Value(std::string("a\0b",3)));
    auto saved_globals=globals.checkpoint();globals.set(0,s::Value(std::int32_t(33)));globals.restore(saved_globals);CHECK(globals.get(0).integer()==9 && std::bit_cast<unsigned>(globals.get(3).real())==0x80000000 && globals.get(4).text().size()==3);++cases;
    globals.set(3,s::Value(std::bit_cast<float>(0x7fc12345u)));auto nan=globals.checkpoint();globals.set(3,s::Value(1.f));globals.restore(nan);CHECK(globals.checkpoint()==nan);++cases;
    auto corrupt=saved_globals;corrupt.back()^=1;auto before=globals.checkpoint();rejects([&]{globals.restore(corrupt);});CHECK(globals.checkpoint()==before);++guards;
    auto bytes_only=s::Globals::parse(text("1,b,0\r\n"));c::Writer bad;bad.u32(1);bad.string("b");bad.u32(1);bad.u32(3);bad.boolean(false);bad.i32(256);
    rejects([&]{bytes_only.restore(bad.seal("LSGL"));});CHECK(bytes_only.get(0).integer()==0);++guards;
    s::Functions functions;functions.load(function(1,{5,3}));functions.load(function(3,{}));functions.load(function(400,{6}));functions.load(function(401,{}));
    Script first;first.op({3,0});first.integer(0);first.op({4,1,6,30,1,2,4,400,6,30});auto root=first.finish("root","child,");
    Script second;second.op({2,0});second.integer(0);second.op({6,27});second.integer(77);second.op({6,0,6,30,1,2});second.integer(8);second.op({6,0,6,30,4,401,6,30,2,0});second.integer(0);second.op({6,27,4,400,6,30});auto child=second.finish("child");
    auto variables=s::Globals::parse(text("1,b,0\r\n2,s,0\r\n3,i,0\r\n"));
    auto loader=[&](std::string_view name){if(name=="root")return root;if(name=="child")return child;throw s::Error("missing synthetic scene");};
    s::Session session(functions,variables,loader);session.start("root");unsigned probes{},waits{};bool complete=false;
    auto host=[&](s::Command& call){
        if(call.index==400){const auto result=call.arguments[0].integer();CHECK(result==(probes?8:77));++probes;return s::CommandResult{true,false,{}};}
        CHECK(call.index==401);++waits;CHECK(call.continuation==0 || call.continuation==1);call.continuation=1;
        rejects([&]{session.checkpoint();});rejects([&]{session.restore(saved_globals);});
        return s::CommandResult{complete,true,{}};
    };
    CHECK(session.advance(host)==s::RunState::yielded && session.scene_depth()==2 && waits==1 && variables.get(2).integer()==8);
    const auto snapshot=session.checkpoint();complete=true;
    CHECK(session.advance(host)==s::RunState::yielded && waits==2);CHECK(session.advance(host)==s::RunState::yielded && probes==1);
    CHECK(session.advance(host)==s::RunState::stopped && probes==2);++cases;
    variables.set(2,s::Value(std::int32_t(999)));session.restore(snapshot);CHECK(variables.get(2).integer()==8 && session.scene_depth()==2 && session.checkpoint()==snapshot);
    probes=0;waits=0;CHECK(session.advance(host)==s::RunState::yielded && waits==1);CHECK(session.advance(host)==s::RunState::yielded && probes==1);CHECK(session.advance(host)==s::RunState::stopped && probes==2);resumed_calls=waits+probes;++cases;
    // Failed restore preserves both the old global variables and every frame.
    const auto stopped=session.checkpoint();auto wrong=snapshot;wrong[16]^=1;rejects([&]{session.restore(wrong);});CHECK(session.checkpoint()==stopped);++guards;
    auto old_child=child;Script changed;changed.integer(2);changed.op({6,30});child=changed.finish("child");
    rejects([&]{session.restore(snapshot);});CHECK(session.checkpoint()==stopped);child=old_child;++guards;
    s::Runtime runtime(root,functions,variables);CHECK(runtime.start(0));const auto initial=runtime.checkpoint();
    const auto name=root->name().size();
    for(auto [field,v]:{std::pair<std::size_t,unsigned>{16+name,24},std::pair<std::size_t,unsigned>{36+name,256},std::pair<std::size_t,unsigned>{24+name,3}}){
        auto invalid=mutated("LSRT",initial,field,v);rejects([&]{runtime.restore(invalid);});CHECK(runtime.checkpoint()==initial);++guards;
    }
    const auto run_once=[&](s::Command&){rejects([&]{runtime.advance({});});rejects([&]{runtime.checkpoint();});return s::CommandResult{false,true,{}};};
    CHECK(runtime.advance(run_once)==s::RunState::yielded);++cases;
    auto alternate=s::Program::parse("root",Bytes{'L','S','C','R',20,0,0,0,1,0,0,0,0,0,0,0,20,0,0,0,0,0,0,0,1,0,0,0},{});
    s::Runtime mismatch(std::make_shared<s::Program>(std::move(alternate)),functions,variables);rejects([&]{mismatch.restore(initial);});++guards;

    auto disk=std::make_shared<Storage>();auto progress=std::make_shared<p::Store>(disk);progress->flush();
    s::SceneChoices choices(progress,[]{return std::string_view("2003");});
    auto add=[&](std::string label,int flag,int expected,int show){std::array<s::Value,4> args={s::Value(std::move(label)),s::Value(flag),s::Value(expected),s::Value(show)};s::Command call{208,args};CHECK(choices.dispatch(call)->complete);};
    add("hidden",1,1,0);add("disabled",2,1,1);add("enabled",3,0,0);
    std::array<s::Value,1> group{s::Value(std::int32_t(2))};s::Command select{209,group};CHECK(!choices.dispatch(select)->complete && choices.active());
    CHECK(!choices.items()[0].visible && choices.items()[1].visible && !choices.items()[1].enabled && choices.items()[2].enabled);
    const auto choice_state=choices.checkpoint();rejects([&]{choices.choose(0);});rejects([&]{choices.choose(1);});guards+=2;
    disk->failing=true;rejects([&]{choices.choose(2);});CHECK(choices.active());disk->failing=false;
    choices.choose(2);CHECK(!choices.active() && progress->state().flag(902)==4);
    auto selected=choices.dispatch(select);CHECK(selected->complete && selected->yield_frame && selected->value->integer()==2 && choices.items().empty());++cases;
    choices.restore(choice_state);CHECK(choices.active() && choices.items().size()==3);choices.choose(2);CHECK(choices.dispatch(select)->value->integer()==2);++cases;
    auto choice_before=choices.checkpoint();auto invalid_choice=choice_state;invalid_choice.pop_back();rejects([&]{choices.restore(invalid_choice);});CHECK(choices.checkpoint()==choice_before);++guards;
    for(unsigned n=0;n<8;++n)add("entry",0,0,0);rejects([&]{add("overflow",0,0,0);});++guards;
    choices.clear();add("all disabled",4,9,1);s::Command unavailable{209,group};rejects([&]{choices.dispatch(unavailable);});CHECK(!choices.active());++guards;
    // Reject labels and checkpoint positions inside an instruction operand.
    Script invalid_label;invalid_label.integer(10);auto raw=std::move(invalid_label.code);put(raw,16,24);rejects([&]{s::Program::parse("bad",std::move(raw),{});});++guards;
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-script-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"resumed_callbacks\":"<<resumed_calls
        <<",\"nested_call_restore\":true,\"pending_command_restore\":true,\"exact_float_bits\":true,\"choice_original_ordinals\":true,\"failed_restore_transactional\":true,\"synthetic_only\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    report.close();CHECK(report);std::cout<<"PASS native script checkpoints, nested calls and conditional choices.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
