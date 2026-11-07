#include "script_legacy.hpp"
#include "checkpoint_codec.hpp"
#include <algorithm>
#include <bit>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>

namespace s=wa2::script;namespace c=wa2::checkpoint;
using Bytes=std::vector<std::uint8_t>;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
std::span<const std::uint8_t> text(std::string_view value){return {reinterpret_cast<const std::uint8_t*>(value.data()),value.size()};}
void put(Bytes& bytes,std::size_t at,std::uint32_t value){for(unsigned n=0;n<4;++n)bytes.at(at+n)=std::uint8_t(value>>(n*8));}
void word(Bytes& bytes,std::uint32_t value){const auto at=bytes.size();bytes.resize(at+4);put(bytes,at,value);}
void string(Bytes& bytes,std::size_t at,std::string_view value){std::copy(value.begin(),value.end(),bytes.begin()+std::ptrdiff_t(at));bytes.at(at+value.size())=0;}
void terminated(Bytes& bytes,std::string_view value){word(bytes,std::uint32_t(value.size()+1));bytes.insert(bytes.end(),value.begin(),value.end());bytes.push_back(0);}
Bytes function(unsigned index,std::initializer_list<std::uint8_t> parameters,std::string_view defaults={}){
    Bytes bytes;word(bytes,index);bytes.resize(124);bytes[4]='f';bytes[36]=std::uint8_t(parameters.size());put(bytes,40,3);std::copy(parameters.begin(),parameters.end(),bytes.begin()+44);
    for(unsigned n=0;n<parameters.size();++n){if(n+1==parameters.size())bytes.insert(bytes.end(),defaults.begin(),defaults.end());bytes.push_back(',');}return bytes;
}
struct Script {
    Bytes bytes{'L','S','C','R'};
    Script(){word(bytes,20);word(bytes,1);word(bytes,0);word(bytes,20);}
    void op(std::initializer_list<std::uint32_t> words){for(auto value:words)word(bytes,value);}
    std::shared_ptr<const s::Program> program(std::string name,std::string_view strings={})const{return std::make_shared<const s::Program>(s::Program::parse(std::move(name),bytes,text(strings)));}
};
Bytes globals(const s::Globals& values,std::span<const unsigned> order){
    Bytes bytes(4);word(bytes,unsigned(order.size()));for(auto n:order){const auto& entry=values.variables()[n];terminated(bytes,entry.name);bytes.push_back(std::uint8_t(entry.storage));
        switch(entry.storage){
        case s::Type::byte:bytes.push_back(std::uint8_t(entry.value.integer()));break;
        case s::Type::short_integer:{const auto v=std::uint32_t(entry.value.integer());bytes.push_back(std::uint8_t(v));bytes.push_back(std::uint8_t(v>>8));break;}
        case s::Type::integer:word(bytes,std::bit_cast<unsigned>(entry.value.integer()));break;
        case s::Type::real:word(bytes,std::bit_cast<unsigned>(entry.value.real()));break;
        case s::Type::text:terminated(bytes,entry.value.text());break;
        }
    }put(bytes,0,unsigned(bytes.size()));return bytes;
}
Bytes frame(const s::Program& program,std::uint32_t offset=20){
    Bytes bytes(0xd190);put(bytes,4,program.version());put(bytes,8,unsigned(program.labels().size()));for(unsigned n=0;n<program.labels().size();++n){put(bytes,12+n*4,std::bit_cast<unsigned>(program.labels()[n].id));put(bytes,0x40c+n*4,program.labels()[n].offset);}
    put(bytes,0x80c,2);put(bytes,0x8cac,offset);put(bytes,0x8b40,std::bit_cast<unsigned>(program.labels().front().id));string(bytes,0xcef8,program.name());string(bytes,0xcf3c,"ck-gal");return bytes;
}
void slot(Bytes& bytes,unsigned index,unsigned kind,unsigned type,unsigned payload,unsigned real=0,unsigned pointer=0){const auto at=0x8cb0+index*20;put(bytes,at,kind);put(bytes,at+4,type);put(bytes,at+8,pointer);put(bytes,at+12,real);put(bytes,at+16,payload);put(bytes,0xa0b0,index);}
Bytes save(std::initializer_list<Bytes> frames,const Bytes& variables,unsigned spare=4){
    CHECK(frames.size()<=spare+1);Bytes bytes(12+(spare+1)*0xd190);string(bytes,0,"1.00");put(bytes,4,spare);put(bytes,8,12+(spare+1)*0xd190);
    unsigned n{};for(const auto& frame:frames){CHECK(frame.size()==0xd190);std::copy(frame.begin(),frame.end(),bytes.begin()+12+std::ptrdiff_t(n++)*0xd190);}bytes.insert(bytes.end(),variables.begin(),variables.end());return bytes;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{},callbacks{},loads{};
    auto schema=s::Globals::parse(text("1,b,2\r\n2,s,3\r\n3,i,4\r\n4,f,0.0\r\n5,t,0\r\n"));auto saved=schema;
    saved.set(0,s::Value(255));saved.set(1,s::Value(-32123));saved.set(2,s::Value(std::numeric_limits<std::int32_t>::min()));saved.set(3,s::Value(std::bit_cast<float>(0x80000000u)));saved.set(4,s::Value(std::string("OWNED\x81\x40")));
    const unsigned order[]{2,4,0,3,1};const auto variables=globals(saved,order);const auto original_schema=schema.checkpoint();
    s::Functions functions;functions.load(function(1,{5,3}));functions.load(function(2,{3}));functions.load(function(128,{3,4,5},"DEFAULT"));functions.load(function(129,{}));functions.load(function(130,{3}));functions.load(function(131,{4}));
    Script stop;stop.op({0,1});auto stopped=stop.program("stop");
    std::map<std::string,std::shared_ptr<const s::Program>,std::less<>> programs{{"stop",stopped}};
    s::Loader loader=[&](auto name){++loads;return programs.at(std::string(name));};
    const auto convert=[&](const Bytes& bytes,s::LegacyResumeText resolver={}){auto result=s::import_legacy_script(bytes,functions,schema,loader,std::move(resolver));CHECK(schema.checkpoint()==original_schema);return result;};
    auto plain=save({frame(*stopped)},variables);auto imported=convert(plain);CHECK(imported.scenes==std::vector<std::string>{"stop"} && !imported.pending_command && imported.consumed==plain.size());auto current=schema;s::Session session(functions,current,loader);session.restore(imported.checkpoint);CHECK(current.checkpoint()==saved.checkpoint() && session.scene_depth()==1 && session.advance({})==s::RunState::stopped);++cases;
    auto nan=saved;nan.set(3,s::Value(std::bit_cast<float>(0x7fc12345u)));const auto nan_import=convert(save({frame(*stopped)},globals(nan,order)));session.restore(nan_import.checkpoint);CHECK(std::bit_cast<unsigned>(current.get(3).real())==0x7fc12345u);++cases;
    auto trailing=plain;trailing.insert(trailing.end(),{7,8,9});CHECK(convert(trailing).consumed==plain.size());++cases;
    auto inactive=frame(*stopped,unsigned(stopped->size()));put(inactive,0x80c,1);session.restore(convert(save({inactive},variables)).checkpoint);CHECK(!session.active()->running() && session.advance({})==s::RunState::stopped);++cases;
    Script probe;probe.op({4,128,6,30,0,1});auto arguments=probe.program("arguments","OWNED,");programs["arguments"]=arguments;
    auto argument_frame=frame(*arguments);slot(argument_frame,1,5,3,73);slot(argument_frame,2,5,4,0,0x80000000);slot(argument_frame,3,3,0xffffffff,0);auto argument_save=save({argument_frame},variables);
    const auto check_arguments=[&](std::string_view wanted){return [&,wanted](s::Command& command){++callbacks;CHECK(command.index==128 && command.arguments.size()==3 && command.arguments[0].integer()==73 && std::bit_cast<unsigned>(command.arguments[1].real())==0x80000000 && command.arguments[2].text()==wanted);return s::CommandResult{true,false,s::Value(0)};};};
    session.restore(convert(argument_save).checkpoint);CHECK(session.advance(check_arguments("OWNED"))==s::RunState::stopped);++cases;
    slot(argument_frame,3,5,5,0xffffffff);session.restore(convert(save({argument_frame},variables)).checkpoint);CHECK(session.advance(check_arguments("DEFAULT"))==s::RunState::stopped);++cases;
    slot(argument_frame,3,5,5,0);session.restore(convert(save({argument_frame},variables)).checkpoint);CHECK(session.advance(check_arguments(""))==s::RunState::stopped);++cases;
    Script local;local.op({6,0,6,30,1,2,4,130,6,30,0,1});auto local_program=local.program("local");programs["local"]=local_program;
    auto local_frame=frame(*local_program);slot(local_frame,1,1,0xffffffff,0,0,0xf0aabcde);slot(local_frame,2,2,3,0);put(local_frame,0xa0b4+3*4,1234);put(local_frame,0x8ca8,19);session.restore(convert(save({local_frame},variables)).checkpoint);
    CHECK(session.advance([&](s::Command& call){++callbacks;CHECK(call.index==130 && call.caller==19 && call.arguments[0].integer()==1234);return s::CommandResult{true,false,{}};})==s::RunState::stopped && current.get(2).integer()==1234);++cases;
    Script reals;reals.op({6,0,6,30,1,3,4,131,6,30,0,1});auto real_program=reals.program("real");programs["real"]=real_program;
    auto real_frame=frame(*real_program);slot(real_frame,1,1,0xffffffff,3);slot(real_frame,2,2,3,26);put(real_frame,0xa734+3*4,0x7fc12345u);session.restore(convert(save({real_frame},variables)).checkpoint);
    CHECK(session.advance([&](s::Command& call){++callbacks;CHECK(call.index==131 && std::bit_cast<unsigned>(call.arguments[0].real())==0x7fc12345u);return s::CommandResult{true,false,{}};})==s::RunState::stopped && std::bit_cast<unsigned>(current.get(3).real())==0x7fc12345u);++cases;
    auto pending=frame(*arguments,28);put(pending,0x8b34,1);put(pending,0x8b38,4);put(pending,0x8b3c,128);put(pending,0x8b44,7);put(pending,0x8b48,1);put(pending,0x8b50,3);put(pending,0x8b5c,91);put(pending,0x8b64,4);put(pending,0x8b6c,0x80000000);put(pending,0x8b78,5);put(pending,0x8b7c,0xf0abcdef);
    const s::LegacyResumeText resolver=[&](const s::Program& program,unsigned id,unsigned n)->std::optional<std::string>{CHECK(program.name()=="arguments" && id==128 && n==2);return "RESTORED";};
    const auto pending_save=save({pending},variables);auto resumed=convert(pending_save,resolver);CHECK(resumed.pending_command==128);session.restore(resumed.checkpoint);
    CHECK(session.advance([&](s::Command& call){++callbacks;CHECK(call.index==128 && call.continuation==7 && call.arguments[0].integer()==91 && std::bit_cast<unsigned>(call.arguments[1].real())==0x80000000 && call.arguments[2].text()=="RESTORED");return s::CommandResult{true,false,{}};})==s::RunState::stopped);++cases;
    Script parent;parent.op({3,0,5,3,0,4,1,6,30,4,129,6,30,0,1});auto parent_program=parent.program("parent","arguments,");programs["parent"]=parent_program;auto parent_frame=frame(*parent_program,48);put(parent_frame,0xd044,0xf0ffabcd);slot(parent_frame,1,5,3,0);auto child_frame=pending;put(child_frame,0xd040,0xfffffff0);
    session.restore(convert(save({parent_frame,child_frame},variables),resolver).checkpoint);CHECK(session.scene_depth()==2);unsigned child_calls{},parent_calls{};const auto nested=[&](s::Command& call){++callbacks;if(call.index==128)++child_calls;else{CHECK(call.index==129);++parent_calls;}return s::CommandResult{true,false,{}};};
    CHECK(session.advance(nested)==s::RunState::yielded && session.scene_depth()==1 && child_calls==1 && parent_calls==0);CHECK(session.advance(nested)==s::RunState::stopped && child_calls==1 && parent_calls==1);++cases;
    for(unsigned kind:{2,3,5,6,7}){
        Script flow;flow.op({0,kind==7?16u:13u,5,3,11,4,130,6,30,0,12,96,5,3,22,4,130,6,30,0,1});auto program=flow.program("flow");CHECK(program->size()==104);programs["flow"]=program;
        auto f=frame(*program);slot(f,1,5,3,kind==7?2:0);put(f,0xcef4,1);const auto at=0xadb4+0x214;put(f,at,kind);put(f,at+12,96);put(f,at+16,68);put(f,at+4,0xffffffff);put(f,at+0x210,0x80000000);
        if(kind==7){put(f,at+8,2);put(f,at+16,68);put(f,at+20,28);put(f,at+0x110,2);put(f,at+0x114,0x80000000);}else{put(f,at+8,0xffffffff);if(kind==5){put(f,at+20,20);put(f,at+24,20);}else{put(f,at+20,0xf0000001);put(f,at+24,0xf0000003);}}
        session.restore(convert(save({f},variables)).checkpoint);CHECK(session.advance([&](s::Command& call){++callbacks;CHECK(call.index==130 && call.arguments[0].integer()==22);return s::CommandResult{true,false,{}};})==s::RunState::stopped);++cases;
    }
    const auto reject=[&](const Bytes& bytes,s::LegacyResumeText resolve={}){const auto before=callbacks;rejects([&]{convert(bytes,std::move(resolve));});CHECK(callbacks==before && schema.checkpoint()==original_schema);++guards;};
    for(unsigned count:{0,1,4,8,11})reject(Bytes(plain.begin(),plain.begin()+count));++cases;
    for(auto [at,v]:{std::pair<std::size_t,unsigned>{0,0},{4,5},{8,12},{12+4,19},{12+8,2},{12+12,9},{12+0x40c,24},{12+0x80c,0},{12+0x80c,3},{12+0x8b40,99},{12+0x8cac,24},{12+0x8cac,0xffffffff},{12+0xa0b0,256},{12+0x8cb0,5},{12+0xcef4,16},{12+0xadb4,2},{12+0xd048,1},{12+0xd044,1},{12+0xd190,7}}){auto bad=plain;put(bad,at,v);reject(bad);}++cases;
    for(auto [offset,v]:{std::pair<std::size_t,unsigned>{0x8cb0+20,0},{0x8cb0+20,4},{0x8cb0+20,7},{0x8cb0+24,6},{0x8cb0+20,1}}){auto bad=argument_save;put(bad,12+offset,v);if(v==1)put(bad,12+0x8cb0+36,1024);reject(bad);}++cases;
    for(auto [offset,v]:{std::pair<std::size_t,unsigned>{0x8cb0+44,16},{0x8cb0+44,0xfffffffe},{0x8cb0+56,52}}){auto bad=local_frame;put(bad,offset,v);reject(save({bad},variables));}++cases;
    {auto bad=argument_frame;slot(bad,3,3,0xffffffff,9999);reject(save({bad},variables));slot(bad,3,5,5,0,0,0xfffffff0);reject(save({bad},variables));slot(bad,3,5,5,9);reject(save({bad},variables));}++cases;
    for(unsigned kind:{1,4,8}){auto bad=frame(*stopped);put(bad,0xcef4,1);put(bad,0xadb4+0x214,kind);reject(save({bad},variables));}++cases;
    {auto bad=frame(*stopped);put(bad,0xcef4,1);const auto at=0xadb4+0x214;put(bad,at,7);put(bad,at+8,65);reject(save({bad},variables));put(bad,at+8,1);put(bad,at+12,20);put(bad,at+16,24);reject(save({bad},variables));put(bad,at+16,20);put(bad,at+12,24);reject(save({bad},variables));}++cases;
    for(auto [at,v]:{std::pair<std::size_t,unsigned>{0x80c,1},{0x8b38,5},{0x8b44,0},{0x8b3c,0},{0x8b3c,127},{0x8b3c,129},{0x8cac,20},{0x8b50,4},{0x8b64,3},{0x8b78,3}}){auto bad=pending;put(bad,at,v);reject(save({bad},variables),resolver);}++cases;
    reject(pending_save);reject(pending_save,[](auto&,auto,auto)->std::optional<std::string>{return {};});reject(pending_save,[](auto&,auto,auto)->std::optional<std::string>{throw std::runtime_error("owned resolver failure");});reject(pending_save,[](auto&,auto,auto)->std::optional<std::string>{return std::string("a\0b",3);});++cases;
    {auto bad=parent_frame;put(bad,0x80c,1);reject(save({bad,child_frame},variables),resolver);bad=pending;put(bad,0xd044,1);reject(save({bad,child_frame},variables),resolver);bad=child_frame;put(bad,0xd044,1);reject(save({parent_frame,bad},variables,1),resolver);}++cases;
    {auto bad=frame(*stopped);string(bad,0xcf3c,"other");reject(save({bad},variables));string(bad,0xcf3c,"ck-gal");string(bad,0xcef8,"../stop");reject(save({bad},variables));std::fill(bad.begin()+0xcef8,bad.begin()+0xcf38,65);reject(save({bad},variables));bad=frame(*stopped);std::fill(bad.begin()+0xcf3c,bad.begin()+0xd03c,65);reject(save({bad},variables));}++cases;
    const auto global_at=12+5*0xd190;
    for(auto [at,v]:{std::pair<std::size_t,unsigned>{0,7},{0,0xffffffff},{4,1025},{4,4},{8,0},{8,4098}}){auto bad=plain;put(bad,global_at+at,v);reject(bad);}++cases;
    {auto bad=plain;bad.at(global_at+14)=1;reject(bad);bad=plain;bad.at(global_at+13)='X';reject(bad);bad=plain;bad.at(global_at+12)='z';reject(bad);bad.pop_back();reject(bad);}++cases;
    {const unsigned duplicates[]{2,2,0,3,1};reject(save({frame(*stopped)},globals(saved,duplicates)));auto wrong=s::Globals::parse(text("3,b,1\r\n2,s,3\r\n3,i,4\r\n4,f,0.0\r\n5,t,0\r\n"));reject(save({frame(*stopped)},globals(wrong,order)));}++cases;
    const auto checked=loads;rejects([&]{s::import_legacy_script(plain,functions,schema,{});});rejects([&]{s::import_legacy_script(plain,functions,schema,[](auto)->std::shared_ptr<const s::Program>{return {};});});rejects([&]{s::import_legacy_script(plain,functions,schema,[&](auto){return arguments;});});CHECK(loads==checked && schema.checkpoint()==original_schema);guards+=3;++cases;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-legacy-script.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"source_global_types\":true,\"global_reference_remapping\":true,\"exact_real_payloads\":true,\"locals_and_flow_translation\":true,\"nested_calls_resume_once\":true,\"suspended_text_requires_resolver\":true,\"obsolete_pointers_not_resolved\":true,\"conversion_does_not_execute\":true,\"conversion_does_not_mutate\":true,\"manual_scene_import_complete\":false,\"synthetic_data_only\":true,\"original_resources_opened\":false,\"original_saves_opened\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS original script-save data converted; synthetic continuations only, no original files.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
