#include "process.hpp"
#include <array>
#include <fstream>
#include <iostream>

#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed at ")+std::to_string(__LINE__)+": "+#x);}while(false)
using wr::U32;
constexpr U32 Entry=0x456650, Counter=0x4be070, Deadline=0x4be074;
int main(int argc,char** argv){try{
    CHECK(argc==2);CHECK(wa2::aot::find(Entry));
    unsigned scenarios{},calls{},dispatches{},guards{},clock_calls{},table_entries{};
    for(bool linear:{false,true})for(bool leaf:{false,true}){
        wr::ProcessOptions options;options.prefer_linear_memory=linear;
        wa2::aot::Runtime runtime(options);auto& p=runtime.process();auto& m=p.memory;
        U32 now=1000;
        p.replace_api("winmm.dll","timeGetTime",0,[&](wr::Process&){++clock_calls;return now;});
        if(leaf)p.certify_leaf_api("winmm.dll","timeGetTime");
        runtime.load();CHECK(!p.gui() && !p.directdraw());
        const auto argument=p.allocate_bytes(128);
        for(unsigned n=0;n<4;++n){CHECK(wa2::aot::find(U32(m.load(0x4c27c0+n*4,32))));++table_entries;}
        CHECK(m.load(0x4c27d0,32)==0 && m.load(0x4c27c0,32)==Entry);
        // Exercise the actual indirect call instruction used for extensions,
        // stopping at its boundary before any script/menu loop can execute.
        {
            wa2::aot::Program program(m);auto cpu=p.cpu;cpu.eip=0x453fdb;cpu.r[wr::EDX]=Entry;
            wr::push(cpu,m,argument);const auto stack=cpu.r[wr::ESP];std::uint64_t budget=1;
            CHECK(program.invoke(cpu,budget));CHECK(cpu.eip==Entry && cpu.r[wr::ESP]==stack-4);
            CHECK(m.load(cpu.r[wr::ESP],32)==0x453fdd && m.load(cpu.r[wr::ESP]+4,32)==argument);++dispatches;
        }
        auto reset=[&](U32 duration,U32 timed,U32 suppress,U32 cancel){
            for(U32 at:std::array<U32,12>{Counter,Deadline,0x4be03c,0x4be018,0x4c2240,0x4be00c,0xa39788,0xa39778,0xa397b0,0x4be048,0xa391f4,0xa391e4})m.store(at,0,32);
            m.store(0xa391e8,0xffffffffu,32);
            std::array<std::uint8_t,128> data;data.fill(0x5a);m.copy_in(argument,data);
            m.store(argument,0xdeadbeefu,32);m.store(argument+0x18,duration,32);m.store(argument+0x2c,timed,32);
            m.store(argument+0x40,suppress,32);m.store(argument+0x54,cancel,32);++scenarios;
        };
        auto call=[&](U32 pending,U32 count){
            std::array<std::uint8_t,128> before,after;m.copy_out(argument,before);
            p.cpu.r[wr::ESI]=0x13579246;p.cpu.r[wr::EDI]=0x24681357;
            const auto saved=p.cpu;
            CHECK(p.callback(U32(m.load(0x4c27c0,32)),std::array<U32,1>{argument},false)==1);
            CHECK(p.cpu.r==saved.r && p.cpu.eip==saved.eip && p.cpu.fp.bytes==saved.fp.bytes);
            CHECK(m.load(argument,32)==pending && m.load(Counter,32)==count);
            m.copy_out(argument,after);CHECK(std::equal(before.begin()+4,before.end(),after.begin()+4));++calls;
        };
        reset(3,0,1,0);const auto old_clock=clock_calls;call(1,1);call(1,2);call(0,0);CHECK(clock_calls==old_clock);
        reset(0,0,1,0);call(1,1);call(0,0);
        reset(100,1,1,0);now=1000;call(1,1);CHECK(m.load(Deadline,32)==1100);
        now=1099;call(1,2);now=1100;call(0,0);
        // The fourth argument is the difference from the already-present
        // three-argument wait callback; it must not be hardcoded to zero.
        for(U32 cancel:{0u,1u,2u}){
            reset(100,0,0,cancel);m.store(0xa397b0,1,32);m.store(0xa391e8,0,32);m.store(0x4be048,1,32);
            call(cancel==1?0:1,cancel==1?0:1);
        }
        reset(100,0,0,1);m.store(0xa397b0,1,32);call(1,1); // input alone cannot skip unread text
        reset(100,0,1,1);m.store(0xa397b0,1,32);m.store(0xa391e8,0,32);m.store(0x4be048,1,32);call(1,1);
        reset(100,0,0,0);m.store(0xa39788,1,32);m.store(0x4be00c,1,32);call(0,0);
        reset(100,0,1,0);m.store(0xa39788,1,32);m.store(0x4be00c,1,32);call(1,1);
        reset(100,0,0,0);m.store(0x4be03c,1,32);m.store(0xa39778,1,32);m.store(0xa391e8,0,32);m.store(0x4be048,1,32);call(0,0);
        // Preserve the source's unsigned deadline comparison even at wrap.
        reset(0x30,1,1,0);now=0xfffffff0u;call(1,1);CHECK(m.load(Deadline,32)==0x20);
        now=0xfffffff1u;call(0,0);
        reset(2,0,1,0);
        const auto saved=p.cpu;const auto code=m.load(Entry,8);
        for(bool changed:{false,true}){
            m.protect(0x456000,4096,wr::Memory::Read|wr::Memory::Write);
            if(changed){m.store(Entry,code^1,8);m.protect(0x456000,4096,wr::Memory::Read|wr::Memory::Execute);}
            bool rejected=false;
            try{p.callback(Entry,std::array<U32,1>{argument},false);}
            catch(const wr::GuestFault& e){rejected=e.kind==wr::FaultKind::unsupported && e.address==Entry;}
            CHECK(rejected && m.load(Counter,32)==0 && m.load(argument,32)==0xdeadbeefu);++guards;p.cpu=saved;
            m.protect(0x456000,4096,wr::Memory::Read|wr::Memory::Write);m.store(Entry,code,8);
            m.protect(0x456000,4096,wr::Memory::Read|wr::Memory::Execute);
        }
        call(1,1);call(0,0); // failed proofs must not poison a later valid call
        wa2::aot::Program program(m);auto unknown=p.cpu;unknown.eip=0x45667b;const auto snapshot=unknown;
        CHECK(!program.invoke_unmetered(unknown) && unknown.r==snapshot.r && unknown.eip==snapshot.eip);++guards;
        CHECK(p.executed_instructions==0);
    }
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.script-wait-callback.v1\",\"entry\":"<<Entry
        <<",\"memory_clock_configurations\":4,\"scenarios\":"<<scenarios<<",\"completed_calls\":"<<calls
        <<",\"indirect_dispatches\":"<<dispatches<<",\"registered_extension_entries\":"<<table_entries
        <<",\"guard_cases\":"<<guards<<",\"controlled_clock_calls\":"<<clock_calls
        <<",\"source_wait_logic_preserved\":true,\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    CHECK(out.good());std::cout<<"PASS shared script wait callback: "<<scenarios<<" scenarios, "<<calls<<" calls.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
