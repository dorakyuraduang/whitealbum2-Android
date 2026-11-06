// Synthetic script/save data only. No game image, AOT functions, device or UI.
#include "progress.hpp"
#include "progress_storage.hpp"
#include "scene_progress.hpp"
#include "script_session.hpp"
#include "test_progress.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <random>

namespace p=wa2::progress;namespace s=wa2::script;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
using Bytes=p::Bytes;
std::span<const std::uint8_t> text(std::string_view v){return {reinterpret_cast<const std::uint8_t*>(v.data()),v.size()};}
void word(Bytes& b,std::uint32_t value){for(unsigned n=0;n<4;++n)b.push_back(std::uint8_t(value>>(n*8)));}
void put(Bytes& b,std::size_t at,std::uint32_t value){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(value>>(n*8));}
void file(const std::filesystem::path& path,std::span<const std::uint8_t> bytes){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());out.close();CHECK(out);}
Bytes file(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);CHECK(in);return {std::istreambuf_iterator<char>(in),{}};}
template<class F>void rejects(F&& f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}CHECK(failed);}
struct MemoryStorage final : p::Storage {
    std::optional<Bytes> disk;
    std::vector<std::pair<p::Block,std::size_t>> writes;
    std::optional<p::Block> fail;
    std::optional<Bytes> load()override{return disk;}
    void write(p::Block block,std::span<const std::uint8_t> bytes)override{
        if(fail==block)throw p::Error("injected persistence failure");
        CHECK(bytes.size()==p::size(block));if(!disk)disk=Bytes(p::layout::system_save_bytes);
        std::copy(bytes.begin(),bytes.end(),disk->begin()+p::offset(block));writes.emplace_back(block,bytes.size());
    }
};
Bytes function(std::uint32_t id,std::initializer_list<std::uint8_t> params){
    Bytes b;word(b,id);b.resize(124);b[4]='f';b[36]=std::uint8_t(params.size());put(b,40,3);
    std::copy(params.begin(),params.end(),b.begin()+44);for(std::size_t n=0;n<params.size();++n)b.push_back(',');return b;
}
struct Script {
    Bytes code;
    Script(){code={'L','S','C','R'};word(code,20);word(code,1);word(code,0);word(code,20);}
    void op(std::initializer_list<std::uint32_t> values){for(auto v:values)word(code,v);}
    void value(std::int32_t v){op({5,3,std::bit_cast<std::uint32_t>(v)});}
    void call(unsigned id,std::initializer_list<std::int32_t> args){for(auto v:args)value(v);op({4,id});}
    void discard(){op({6,30});}
    std::shared_ptr<const s::Program> finish(){op({0,1});return std::make_shared<s::Program>(s::Program::parse("synthetic-progress",std::move(code),{}));}
};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned format_cases{},flag_cases{},gallery_cases{},history_cases{},guard_cases{},script_calls{},io_cases{};
    // Known external byte layout, with unrelated/reserved fields randomized.
    Bytes source(2528384);std::mt19937 random(0x50524f47);for(auto& b:source)b=std::uint8_t(random());
    auto state=p::State::decode(source);CHECK(state.encode()==source);++format_cases;
    for(auto block:{p::Block::history,p::Block::gallery,p::Block::flags})CHECK(!state.dirty(block));
    for(auto length:{std::size_t(0),std::size_t(2528383),std::size_t(2528385)}){Bytes bad(length);rejects([&]{p::State::decode(bad);});++guard_cases;}
    for(auto index:{0u,98u,99u,100u,900u,1023u})for(auto value:{0,1,-1,std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()}){
        const auto expected_value=index==99?0:value;state.set_flag(index,value);put(source,0x268480+index*4,std::bit_cast<std::uint32_t>(expected_value));
        CHECK(state.flag(index)==expected_value && state.encode()==source);++flag_cases;
    }
    for(auto bank:{0u,1u})for(auto index:{0u,1u,100100u,999999u})for(auto value:{0u,1u,128u,255u}){
        state.set_gallery(bank,index,std::uint8_t(value));source[0x80000+bank*1000000+index]=std::uint8_t(value);
        CHECK(state.gallery(bank,index)==value && state.encode()==source);++gallery_cases;
    }
    p::State history;
    for(auto script:{0u,1u,511u,1023u})for(auto line:{0u,1u,7u,8u,4095u}){
        CHECK(!history.read(script,line));history.mark_read(script,line);CHECK(history.read(script,line));
        const auto data=history.block(p::Block::history);CHECK(data[script*512+line/8]&(0x80u>>(line%8)));++history_cases;
    }
    for(auto index:{1024u,0xffffffffu}){rejects([&]{state.flag(index);});rejects([&]{state.set_flag(index,1);});guard_cases+=2;}
    rejects([&]{state.gallery(2,0);});rejects([&]{state.set_gallery(0,1000000,1);});
    rejects([&]{state.read(1024,0);});rejects([&]{state.mark_read(0,4096);});
    rejects([&]{state.block(p::Block(3));});rejects([&]{state.dirty(p::Block(3));});guard_cases+=6;CHECK(state.encode()==source);
    // Test unlock shares the existing catalogue but has independent state.
    auto unlocked=state;unlocked.unlock_all_for_testing();auto expected=source;wa2::testing::unlock_all_content(expected);
    CHECK(unlocked.encode()==expected && state.encode()==source);++format_cases;
    auto loaded_unlock=p::State::decode(expected);loaded_unlock.unlock_all_for_testing();
    for(auto kind:{p::Block::history,p::Block::gallery,p::Block::flags})CHECK(!loaded_unlock.dirty(kind));++format_cases;

    auto memory=std::make_shared<MemoryStorage>();memory->disk=source;auto store=std::make_shared<p::Store>(memory);
    s::SceneProgress commands(store);
    const auto invoke=[&](unsigned id,std::initializer_list<std::int32_t> values){std::vector<s::Value> args;for(auto v:values)args.emplace_back(v);
        s::Command call{id,args};return commands.dispatch(call);};
    memory->writes.clear();invoke(199,{17,0x12345678});CHECK(memory->writes.size()==1 && memory->writes[0]==std::make_pair(p::Block::flags,std::size_t(4096)));
    CHECK(invoke(198,{17})->value->integer()==0x12345678);invoke(199,{17,0x12345678});store->flush();CHECK(memory->writes.size()==1);
    invoke(201,{10010,1,257});invoke(202,{10010,1,-1});CHECK(store->state().gallery(0,100101)==1 && store->state().gallery(1,100101)==255);
    CHECK(memory->writes.size()==3 && memory->writes[1].second==2000000 && memory->writes[2].second==2000000);
    CHECK(!invoke(100,{}));++io_cases;
    const auto before_bad=store->state().encode();
    for(auto id:{198u,199u,201u,202u}){rejects([&]{invoke(id,{});});++guard_cases;}
    rejects([&]{invoke(201,{100000,0,1});});rejects([&]{invoke(202,{-1,0,1});});
    rejects([&]{invoke(201,{std::numeric_limits<std::int32_t>::max(),std::numeric_limits<std::int32_t>::max(),1});});guard_cases+=3;
    CHECK(store->state().encode()==before_bad);
    s::SceneProgress absent({});s::Command missing{198,{}};rejects([&]{absent.dispatch(missing);});++guard_cases;
    memory->fail=p::Block::flags;rejects([&]{invoke(199,{18,45});});CHECK(store->state().flag(18)==45 && store->state().dirty(p::Block::flags));
    memory->fail.reset();store->flush();CHECK(!store->state().dirty(p::Block::flags) && p::State::decode(*memory->disk).flag(18)==45);++io_cases;
    // New saves retry unfinished blocks without rewriting acknowledged blocks.
    auto fresh=std::make_shared<MemoryStorage>();p::Store first(fresh);fresh->fail=p::Block::gallery;
    rejects([&]{first.flush();});CHECK(fresh->writes.size()==1 && !first.state().dirty(p::Block::history) && first.state().dirty(p::Block::gallery));
    fresh->fail.reset();first.flush();CHECK(fresh->writes.size()==3 && *fresh->disk==Bytes(2528384));++io_cases;

    // Execute constructed LSCR through the native Session, not direct mocks.
    s::Functions functions;functions.load(function(198,{3}));functions.load(function(199,{3,3}));
    functions.load(function(201,{3,3,3}));functions.load(function(202,{3,3,3}));functions.load(function(400,{3}));
    auto globals=s::Globals::parse(text(""));Script script;
    script.call(199,{44,-12345});script.discard();script.call(198,{44});script.op({4,400});script.discard();
    script.call(201,{20000,3,129});script.discard();script.call(202,{20000,3,3});script.discard();
    const auto program=script.finish();s::Session session(functions,globals,[&](std::string_view name){CHECK(name=="synthetic-progress");return program;});
    session.start("synthetic-progress");
    CHECK(session.advance([&](s::Command& call){++script_calls;
        if(call.index==400){CHECK(call.arguments[0].integer()==-12345);return s::CommandResult{true,false,{}};}
        auto value=commands.dispatch(call);CHECK(value);return *value;})==s::RunState::stopped);
    CHECK(script_calls==5 && store->state().gallery(0,200003)==129 && store->state().gallery(1,200003)==3);
    p::Store restarted(memory);CHECK(restarted.state().flag(44)==-12345 && restarted.state().gallery(1,200003)==3);++io_cases;

    // Filesystem adapter checks use a newly created directory and synthetic
    // files only; no ordinary user save or original resource is opened.
    const auto root=std::filesystem::absolute(std::filesystem::path(argv[1])).parent_path()/("progress-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(std::filesystem::create_directory(root));const Bytes sentinel{3,1,4,1,5};file(root/"save_Q.sav",sentinel);file(root/"SYSTEM.ini",sentinel);
    auto disk=wa2::platform::progress_storage(root);p::Store persisted(disk);persisted.state().set_flag(20,1);persisted.flush();
    CHECK(file(root/"Sys.sav").size()==2528384);p::Store reopened(disk);CHECK(reopened.state().flag(20)==1);++io_cases;
    file(root/"Sys.sav",source);p::Store retained(disk);retained.state().set_flag(29,0x67abcdef);retained.flush();auto expected_disk=source;put(expected_disk,0x268480+29*4,0x67abcdef);
    CHECK(file(root/"Sys.sav")==expected_disk && file(root/"save_Q.sav")==sentinel && file(root/"SYSTEM.ini")==sentinel);++io_cases;
    const auto time=std::filesystem::last_write_time(root/"Sys.sav");retained.flush();CHECK(std::filesystem::last_write_time(root/"Sys.sav")==time);++io_cases;
    const auto previous=file(root/"Sys.sav");rejects([&]{disk->write(p::Block::flags,sentinel);});CHECK(file(root/"Sys.sav")==previous);++guard_cases;
    file(root/"Sys.sav",sentinel);rejects([&]{p::Store bad(disk);});rejects([&]{disk->write(p::Block::flags,state.block(p::Block::flags));});CHECK(file(root/"Sys.sav")==sentinel);guard_cases+=2;
    // Use a fresh name: Windows may keep a just-deleted file pending while an
    // external indexer releases it. This still exercises a real hard link.
    const auto linked=root/"linked-save";CHECK(std::filesystem::create_directory(linked));
    std::filesystem::create_hard_link(root/"save_Q.sav",linked/"Sys.sav");auto link_disk=wa2::platform::progress_storage(linked);
    rejects([&]{p::Store bad(link_disk);});CHECK(file(root/"save_Q.sav")==sentinel);++guard_cases;
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-progress.v1\",\"format_cases\":"<<format_cases<<",\"flag_cases\":"<<flag_cases
        <<",\"gallery_cases\":"<<gallery_cases<<",\"history_cases\":"<<history_cases<<",\"guard_cases\":"<<guard_cases<<",\"script_calls\":"<<script_calls<<",\"io_cases\":"<<io_cases
        <<",\"flag_write_bytes\":4096,\"serialized_bytes\":2528384,\"failed_writes_retryable\":true,\"original_resources_opened\":false,\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    report.close();CHECK(report);std::cout<<"PASS native progress, native LSCR dispatch, save isolation and bounded persistence.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
