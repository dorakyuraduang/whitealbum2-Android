// The reference runs only previously generated instruction functions in memory.
// No source EXE, game process, window, input injection or benchmark is used.
#include "script_runtime.hpp"
#include "script_session.hpp"
#include "scene_controls.hpp"
#include "file_io.hpp"
#include <winrecomp/compiled.hpp>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

bool code_page_1105(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1106(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1140(wr::Cpu&,wr::Memory&,std::uint64_t&);
namespace s=wa2::script;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
using Bytes=std::vector<std::uint8_t>;
void word(Bytes& data,std::uint32_t value){for(unsigned n=0;n<4;++n)data.push_back(std::uint8_t(value>>(n*8)));}
void set_word(Bytes& data,std::size_t at,std::uint32_t value){for(unsigned n=0;n<4;++n)data.at(at+n)=std::uint8_t(value>>(n*8));}
std::span<const std::uint8_t> bytes(std::string_view text){return {reinterpret_cast<const std::uint8_t*>(text.data()),text.size()};}
Bytes function(std::uint32_t index,std::string_view name,std::initializer_list<std::uint8_t> parameters){
    Bytes data;word(data,index);data.resize(124);std::copy(name.begin(),name.end(),data.begin()+4);
    data[36]=std::uint8_t(parameters.size());set_word(data,40,3);std::copy(parameters.begin(),parameters.end(),data.begin()+44);
    for(std::size_t i=0;i<parameters.size();++i)data.push_back(',');return data;
}
struct Builder {
    Bytes code;
    Builder(){code.insert(code.end(),{'L','S','C','R'});word(code,20);word(code,1);word(code,0);word(code,20);}
    void op(std::initializer_list<std::uint32_t> words){for(auto value:words)word(code,value);}
    void value(const s::Value& value){op({5,std::uint32_t(value.type()),value.type()==s::Type::real?std::bit_cast<std::uint32_t>(value.real()):std::uint32_t(value.integer())});}
    std::shared_ptr<const s::Program> finish(){op({4,400,0,1});return std::make_shared<const s::Program>(s::Program::parse("synthetic",std::move(code),{}));}
    std::shared_ptr<const s::Program> scene(std::string name,std::string_view text={}){
        return std::make_shared<const s::Program>(s::Program::parse(std::move(name),std::move(code),bytes(text)));
    }
};
struct Reference {
    wr::Memory memory;
    static constexpr std::uint32_t context=0x100000,stack=0x200000,stream=0x300000,done=0x0badf00d;
    explicit Reference(const std::filesystem::path& fixture){
        memory.map(context,0xe000,3);memory.map(stack,0x4000,3);memory.map(stream,0x1000,3);
        memory.map(0xbc1000,0x8000,3);
        // Generated callers cache import pointers even when both temporary
        // string operands are null. The test never invokes these imports.
        memory.map(0x4a1000,0x1000,3);
        memory.store(0x4a1074,0xf0000000,32);memory.store(0x4a1078,0xf0000010,32);
        // Existing 00451a60.c records the increment/decrement constant as 1.0.
        // This fixture contains code pages only, so supply that read-only datum.
        memory.store(0x4a1e10,0,32);memory.store(0x4a1e14,0x3ff00000,32);
        std::ifstream input(fixture,std::ios::binary);CHECK(input);
        for(auto page:{0x451000u,0x452000u,0x474000u}){
            std::array<std::uint8_t,4096> data{};input.read(reinterpret_cast<char*>(data.data()),data.size());CHECK(input);
            memory.map(page,4096,wr::Memory::Read|wr::Memory::Execute);memory.initialize(page,data);
        }
        memory.store(context+0x810,stream,32);memory.store(0xbc8310,1,32);
    }
    void slot(unsigned index,const s::Value& value,bool variable=false){
        const auto at=context+0x8cb0+index*20;
        memory.store(at,variable?1u:5u,32);memory.store(at+4,std::uint32_t(value.type()),32);
        memory.store(at+8,0,32);memory.store(at+12,value.type()==s::Type::real?std::bit_cast<std::uint32_t>(value.real()):0,32);
        memory.store(at+16,variable?0u:value.type()==s::Type::integer?std::uint32_t(value.integer()):0,32);
    }
    s::Value execute(std::uint32_t operation,const s::Value& left,const s::Value& right,
                     bool unary=false,std::optional<s::Type> storage={}){
        memory.store(context+0x8cac,0,32);memory.store(stream,operation,32);
        const bool reference=storage.has_value();slot(1,left,reference);if(!unary)slot(2,right);
        memory.store(context+0xa0b0,unary?1u:2u,32);
        if(storage){
            memory.store(0xbc1644,std::uint32_t(*storage),8);
            memory.store(0xbc1645,std::uint32_t(left.integer()),8);memory.store(0xbc1646,std::uint32_t(left.integer()),16);
            memory.store(0xbc1648,std::uint32_t(left.integer()),32);memory.store(0xbc164c,std::bit_cast<std::uint32_t>(left.real()),32);
        }
        wr::Cpu cpu;cpu.eip=0x451a60;cpu.r[wr::ECX]=context;cpu.r[wr::ESP]=stack+0x3000;
        memory.store(cpu.r[wr::ESP],done,32);std::uint64_t budget=10000;
        unsigned count=0;
        while(cpu.eip!=done){
            CHECK(++count<10000);bool translated=false;
            if(cpu.eip==0x450800){
                // The optional global-debug callback has no module in normal
                // play. It does not change the script variable under test.
                cpu.eip=wr::pop(cpu,memory);translated=true;
            }else switch(cpu.eip>>12){
            case 0x451:translated=code_page_1105(cpu,memory,budget);break;
            case 0x452:translated=code_page_1106(cpu,memory,budget);break;
            case 0x474:translated=code_page_1140(cpu,memory,budget);break;
            }
            if(!translated)throw std::runtime_error("reference target "+std::to_string(cpu.eip));
        }
        CHECK(memory.load(context+0xa0b0,32)==1);
        if(storage){
            switch(*storage){
            case s::Type::byte:return s::Value(std::int32_t(memory.load(0xbc1645,8)));
            case s::Type::short_integer:return s::Value(std::int32_t(std::bit_cast<std::int16_t>(std::uint16_t(memory.load(0xbc1646,16)))));
            case s::Type::integer:return s::Value(std::bit_cast<std::int32_t>(memory.load(0xbc1648,32)));
            case s::Type::real:return s::Value(std::bit_cast<float>(memory.load(0xbc164c,32)));
            default:throw std::runtime_error("unsupported reference storage");
            }
        }
        const auto at=context+0x8cb0+20;
        const auto result_type=memory.load(at+4,32);
        return result_type==4?s::Value(std::bit_cast<float>(memory.load(at+12,32))):s::Value(std::bit_cast<std::int32_t>(memory.load(at+16,32)));
    }
};
s::Value native(std::uint32_t operation,const s::Value& left,const s::Value& right,
                bool unary=false,std::optional<s::Type> storage={}){
    s::Functions functions;functions.load(function(400,"probe",{6}));
    auto globals=s::Globals::parse(bytes(storage?std::to_string(unsigned(*storage))+",v,"+
        (left.type()==s::Type::real?std::to_string(left.real()):std::to_string(left.integer()))+"\r\n":""));
    Builder builder;
    if(storage)builder.op({1,0});else builder.value(left);
    if(!unary)builder.value(right);builder.op({6,operation});
    auto program=builder.finish();s::Runtime runtime(program,functions,globals);CHECK(runtime.start(0));
    std::optional<s::Value> result;
    CHECK(runtime.advance([&](s::Command& call){CHECK(call.index==400 && call.arguments.size()==1);result=call.arguments[0];return s::CommandResult{true,false,{}};})==s::RunState::stopped);
    CHECK(result);return *result;
}
void same(const s::Value& actual,const s::Value& expected,const std::string& label){
    bool equal=actual.type()==expected.type();
    if(equal && actual.type()==s::Type::real)equal=std::bit_cast<std::uint32_t>(actual.real())==std::bit_cast<std::uint32_t>(expected.real());
    else if(equal)equal=actual==expected;
    if(!equal){
        std::cerr<<label<<" actual "<<unsigned(actual.type())<<":"<<actual.real()<<" expected "<<unsigned(expected.type())<<":"<<expected.real()<<'\n';
        throw std::runtime_error("generated-code script semantic mismatch");
    }
}
int main(int argc,char** argv){try{
    if(argc!=4)throw std::runtime_error("usage: native_script resource-directory generated-pages.bin report.json");
    Reference reference(argv[2]);unsigned comparisons=0,checks=0;
    const std::array<std::pair<std::int32_t,std::int32_t>,7> pairs{{{0,1},{1,1},{-9,4},{100,-7},{0x7fffffff,2},{-12345,31},{-2147483647,3}}};
    for(std::uint32_t operation=8;operation<=22;++operation)for(const auto [a,b]:pairs){
        same(native(operation,s::Value(a),s::Value(b)),reference.execute(operation,s::Value(a),s::Value(b)),"integer op "+std::to_string(operation));++comparisons;
    }
    for(auto operation:{23u,24u})for(auto value:{0,1,-1,0x7fffffff,std::numeric_limits<std::int32_t>::min()}){
        same(native(operation,s::Value(value),s::Value{},true),reference.execute(operation,s::Value(value),s::Value{},true),"unary integer");++comparisons;
    }
    for(auto operation:{9u,10u,13u,14u,16u,17u,18u,19u})for(const auto [a,b]:std::array<std::pair<float,float>,5>{{{0.0f,1.0f},{-1.25f,3.5f},{1234.25f,-33.25f},{1.0e-5f,1.0e5f},{-0.0f,2.0f}}}){
        same(native(operation,s::Value(a),s::Value(b)),reference.execute(operation,s::Value(a),s::Value(b)),"real op "+std::to_string(operation));++comparisons;
    }
    for(auto storage:{s::Type::byte,s::Type::short_integer,s::Type::integer,s::Type::real}){
        for(std::uint32_t operation=0;operation<=7;++operation){
            if(storage==s::Type::real && operation>4)continue;
            const s::Value initial(std::int32_t(255)),right(std::int32_t(3));
            same(native(operation,initial,right,false,storage),reference.execute(operation,initial,right,false,storage),"assignment "+std::to_string(operation)+" storage "+std::to_string(unsigned(storage)));++comparisons;
        }
        for(auto operation:{25u,26u}){
            const s::Value initial(std::int32_t(0));
            same(native(operation,initial,s::Value{},true,storage),reference.execute(operation,initial,s::Value{},true,storage),"increment/decrement");++comparisons;
        }
    }
    // Native command suspension: the same owned arguments survive every frame;
    // a resumed command must neither reread code nor pop the argument stack.
    {
        s::Functions functions;functions.load(function(401,"wait",{3,5}));functions.load(function(400,"probe",{6}));
        auto globals=s::Globals::parse({});Builder builder;builder.value(s::Value(std::int32_t(17)));builder.op({5,5,0xffffffff,4,401});
        const auto program=builder.finish();s::Runtime runtime(program,functions,globals);CHECK(runtime.start(0));
        unsigned calls=0,probes=0;std::uint32_t suspended_offset=0;
        auto host=[&](s::Command& call){
            if(call.index==400){++probes;CHECK(call.arguments[0].integer()==42);return s::CommandResult{true,false,{}};}
            CHECK(call.index==401 && call.arguments[0].integer()==17 && call.arguments[1].text().empty());
            CHECK(call.continuation==calls);++calls;++call.continuation;
            return s::CommandResult{calls==3,true,s::Value(std::int32_t(42))};
        };
        CHECK(runtime.advance(host)==s::RunState::yielded);suspended_offset=runtime.offset();
        CHECK(runtime.advance(host)==s::RunState::yielded && runtime.offset()==suspended_offset);
        CHECK(runtime.advance(host)==s::RunState::yielded && runtime.offset()==suspended_offset);
        CHECK(runtime.advance(host)==s::RunState::stopped && calls==3 && probes==1);++checks;
    }
    // Scene calls have private locals/stacks and shared globals. A child starts
    // on the call's frame; its parent resumes on the update after child cleanup.
    {
        s::Functions functions;
        functions.load(function(0,"SLoad",{5,3}));functions.load(function(1,"SCall",{5,3}));
        functions.load(function(3,"run",{}));functions.load(function(400,"probe",{6}));
        auto globals=s::Globals::parse(bytes("3,v,0\r\n"));
        Builder a;a.op({3,0,5,3,0,4,1,6,30,1,0,4,400,6,30,0,1});
        Builder b;b.op({1,0,5,3,7,6,0,6,30,4,3,6,30,0,1});
        auto first=a.scene("A","B,"),second=b.scene("B");
        s::Session session(functions,globals,[&](std::string_view name){if(name=="A")return first;if(name=="B")return second;throw s::Error("missing test scene");});
        unsigned probes=0;
        auto host=[&](s::Command& call){CHECK(call.index==400 && call.arguments[0].integer()==7);++probes;return s::CommandResult{true,false,{}};};
        session.start("A");CHECK(session.advance(host)==s::RunState::yielded);
        CHECK(session.scene_depth()==2 && globals.get(0).integer()==7 && probes==0);
        CHECK(session.advance(host)==s::RunState::yielded && session.scene_depth()==1 && probes==0);
        CHECK(session.advance(host)==s::RunState::stopped && probes==1);++checks;
        // A scene replacement commits only after the command returns. It starts
        // the new root on the following frame, with no surviving child/locals.
        Builder c;c.op({3,0,5,3,0,4,0,0,1});first=c.scene("A","B,");
        globals.set(0,s::Value(std::int32_t(0)));session.start("A");
        CHECK(session.advance(host)==s::RunState::yielded && session.scene_depth()==1);
        CHECK(session.active()->program().name()=="B" && globals.get(0).integer()==0);
        CHECK(session.advance(host)==s::RunState::yielded && globals.get(0).integer()==7);++checks;
    }
    // A busy script yields at its operation limit instead of blocking a mobile
    // event/render thread. This bound counts LSCR operations, not CPU steps.
    {
        s::Functions functions;auto globals=s::Globals::parse({});Builder b;b.op({0,12,20});
        s::Runtime runtime(b.scene("loop"),functions,globals);CHECK(runtime.start(0));
        CHECK(runtime.advance({},32)==s::RunState::slice_exhausted && runtime.operations()==32 && runtime.offset()==20);++checks;
    }
    // Native if/else paths, including flow-frame cleanup at a branch's end.
    for(auto condition:{0,1}){
        s::Functions functions;functions.load(function(400,"probe",{6}));auto globals=s::Globals::parse({});
        Builder b;b.op({0,2,0,0});const auto alternate_at=28u,end_at=32u;
        b.value(s::Value(std::int32_t(condition)));b.op({0,13});
        b.value(s::Value(std::int32_t(111)));b.op({4,400,6,30,0,12,0});const auto goto_at=b.code.size()-4;
        const auto alternate=std::uint32_t(b.code.size());b.value(s::Value(std::int32_t(222)));b.op({4,400,6,30});
        const auto end=std::uint32_t(b.code.size());b.op({0,1});
        set_word(b.code,alternate_at,alternate);set_word(b.code,end_at,end);set_word(b.code,goto_at,end);
        s::Runtime runtime(b.scene("if"),functions,globals);CHECK(runtime.start(0));unsigned calls=0;
        CHECK(runtime.advance([&](s::Command& call){CHECK(call.arguments[0].integer()==(condition?111:222));++calls;return s::CommandResult{true,false,{}};})==s::RunState::stopped && calls==1);++checks;
    }
    // Real data through the ordinary archive and script engine, without x86
    // instruction execution. Stop at native host boundaries, never skip them.
    // Existing game startup explicitly selects the ck-gal bank. Its byte-coded
    // translated text uses the supplied custom font atlas; do not substitute
    // the base script bank or decode it into a system Japanese font.
    auto archive=wa2::resource::Archive::open(wa2::platform::open_resource_file(std::filesystem::path(argv[1])/"ck-gal.pak"));
    std::vector<std::string> scene_names;
    for(const auto& member:archive.members())if(member.name.ends_with(".bnr"))scene_names.push_back(member.name.substr(0,member.name.size()-4));
    s::Library library(std::move(archive));
    std::uint64_t scene_bytes=0,script_instructions=0;std::array<std::uint64_t,512> command_usage{};unsigned all_scenes=0;
    for(const auto& name:scene_names){
        const auto program=library.load(name);++all_scenes;scene_bytes+=program->size();
        std::uint32_t at=12+std::uint32_t(program->labels().size()*8);
        auto next=[&]{const auto value=program->word(at);at+=4;return value;};
        while(at<program->size()){
            ++script_instructions;const auto kind=next();
            switch(kind){
            case 0:{
                const auto control=next();CHECK(control<=16);
                const unsigned count=control==2?2:control==3||control==6||control==12?1:control==5?4:0;
                if(control==7){const auto cases=next();CHECK(cases<=64);for(unsigned i=0;i<cases*2+1;++i)next();}
                else for(unsigned i=0;i<count;++i)next();
                break;
            }
            case 1:case 2:case 3:next();break;
            case 4:{const auto index=next();library.functions().at(index);++command_usage.at(index);break;}
            case 5:{const auto value_type=next(),value=next();CHECK(value_type>=1 && value_type<=5);if(value_type==5)CHECK(value==0xffffffffu);break;}
            case 6:CHECK(next()<=30);break;
            default:throw std::runtime_error("unknown static LSCR shape in "+name+" at "+std::to_string(at-4));
            }
        }
    }
    const auto scene=library.load("1001");CHECK(scene->version()==20);CHECK(scene->label(0)==28);
    CHECK(library.functions().at(137).name=="VI");CHECK(library.globals().variables().size()>0);
    s::Runtime runtime(scene,library.functions(),library.globals());CHECK(runtime.start(0));
    unsigned prefix_calls=0;
    const auto state=runtime.advance([&](s::Command& call){
        if(prefix_calls==0){CHECK(call.index==3 && call.arguments.empty());++prefix_calls;return s::CommandResult{true,false,{}};}
        CHECK(call.index==137 && call.arguments.size()==2);
        CHECK(call.arguments[0].integer()==0 && call.arguments[1].integer()==1001);++prefix_calls;
        return s::CommandResult{false,true,{}};
    });
    CHECK(state==s::RunState::yielded && prefix_calls==2);++checks;
    {
        s::Session session(library);session.start("1001");
        CHECK(session.advance({})==s::RunState::yielded); // Actual built-in run.
        unsigned blocked=0;
        CHECK(session.advance([&](s::Command& call){CHECK(call.index==137);++blocked;return s::CommandResult{false,true,{}};})==s::RunState::yielded && blocked==1);
        CHECK(session.advance([&](s::Command& call){CHECK(call.index==137);++blocked;return s::CommandResult{false,true,{}};})==s::RunState::yielded && blocked==2);++checks;
    }
    std::uint32_t next_engine_command=0;
    {
        // Actual VI now crosses into ordinary native gameplay state. Leave the
        // following audio/graphics command pending until its real backend exists.
        s::SceneControls controls([]{return 100u;});s::Session session(library);session.start("1001");
        CHECK(session.advance({})==s::RunState::yielded);
        CHECK(session.advance([&](s::Command& call){if(auto value=controls.dispatch(call))return *value;
            next_engine_command=call.index;return s::CommandResult{false,true,{}};})==s::RunState::yielded);
        CHECK(controls.modes().voice_chapter==1001 && next_engine_command==159);++checks;
    }
    std::ofstream report(argv[3]);report<<"{\"schema\":\"wa2.native-script.v1\",\"generated_code_numeric_comparisons\":"<<comparisons
        <<",\"native_boundary_checks\":"<<checks<<",\"real_script_scene\":\"1001\",\"scene_bytes\":"<<scene->size()
        <<",\"scene_text_entries\":"<<scene->strings().size()<<",\"global_variables\":"<<library.globals().variables().size()
        <<",\"script_bank\":\"ck-gal.pak\",\"text_kept_in_original_game_encoding\":true"
        <<",\"parsed_scene_resources\":"<<all_scenes<<",\"parsed_scene_bytes\":"<<scene_bytes<<",\"validated_script_instruction_shapes\":"<<script_instructions
        <<",\"first_engine_command\":137,\"first_engine_command_implemented\":true,\"next_unimplemented_engine_command\":"<<next_engine_command<<",\"complete_gameplay_verified\":false"
        <<",\"reference_optional_debug_callback_omitted\":true,\"original_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"passed\":true}\n";
    report.close();CHECK(report);std::cout<<"PASS "<<comparisons<<" numeric semantics against existing generated code; native archive/scene prefix and command suspension. No game or screen access.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
