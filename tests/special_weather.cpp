// Bounded cached Main/SC weather updates, original/static vs shared transaction.
// Invented RAM only. No startup, resources, windows, device or frame loop.
#include "../recompiled/special/runtime.hpp"
#include "../recompiled/special/weather.hpp"
#include "../recompiled/process.hpp"
#include "../recompiled/native_weather.hpp"
#include "../recompiled/fp_registers.hpp"
#include "weather_profile.hpp"
#include <winrecomp/win32_state.hpp>
#include <bit>
#include <cfenv>
#include <cstring>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("weather check ")+std::to_string(__LINE__)+": " #x);}while(false)
namespace a=wa2::aot;namespace s=wa2::special;using wr::U32;
struct Result {wr::Cpu cpu;std::vector<std::uint8_t> bytes;std::uint64_t batches{},yields{};unsigned sprites{};};
bool source_oracle(wr::Cpu& s,wr::Memory& m,std::uint64_t& budget){switch(s.eip){
#include "weather_oracle.inc"
default:return false;}}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned main_cases{},special_cases{},guards{},prefixes{},accepted{},live_prefixes{};
    for(bool main_profile:{false,true})for(bool linear:{false,true}){
        wr::ProcessOptions options;options.prefer_linear_memory=linear;
        std::unique_ptr<a::Runtime> main;std::unique_ptr<s::Runtime> special;
        if(main_profile){main=std::make_unique<a::Runtime>(options);main->load();}else{special=std::make_unique<s::Runtime>(options);special->process().load(s::initial_image());}
        auto& p=main_profile?main->process():special->process();auto& m=p.memory;
        const auto source=main_profile?a::main_weather_source:s::weather_source;
        const U32 entry=main_profile?0x4078b0:0x407570,tweens=main_profile?0xb4fd58:0xb4cc98;
        const U32 sse=main_profile?0xbc830c:0xbc54f0,image=(main_profile?0xa93fb8:0xa90ef8)+110*128;
        const U32 stack=p.cpu.r[wr::ESP]-0x8000,returning=0xf0001234;
        auto base=p.cpu;base.r[wr::ESP]=stack;
        a::Program main_program(m);a::StaticProgram special_program(m,s::functions(),s::locate);
        const auto setf=[&](U32 at,float value){m.store(at,std::bit_cast<U32>(value),32);};
        const std::array<std::pair<U32,unsigned>,4> ranges{{{source.weather,0x5000},{source.objects,0x80000},{tweens,2048*92},{stack-512,1024}}};
        const auto snapshot=[&]{std::vector<std::uint8_t> bytes;for(auto [at,size]:ranges){const auto start=bytes.size();bytes.resize(start+size);m.copy_out(at,std::span(bytes).subspan(start,size));}return bytes;};
        const auto prepare=[&](unsigned count,unsigned flags,unsigned vectors,unsigned control){
            for(auto [at,size]:ranges)m.copy_in(at,std::vector<std::uint8_t>(size));
            m.store(sse,vectors,32);m.store(sse+4,vectors,32);
            for(auto [offset,value]:{std::pair{0x930u,1u},{0x934u,3u},{0x938u,0u},{0x940u,1u},{0x944u,flags},
                {0x47d4u,count},{0x47e4u,count},{0x47d8u,0u},{0x47e8u,0u},{0x47ecu,0u},{0x47dcu,1u},{0x47e0u,3u}})m.store(source.weather+offset,value,32);
            setf(source.weather+0x47cc,1);setf(source.weather+0x47d0,3);
            m.store(image+0x5c,32,16);m.store(image+0x5e,64,16);
            m.store(image,1,8);for(unsigned offset:{0x60u,0x62u})m.store(image+offset,256,16);
            for(unsigned offset:{0x64u,0x66u,0x68u})m.store(image+offset,1,16);
            for(unsigned n=0;n<count;++n){const auto at=source.weather+0x948+n*40;
                m.store(at,1,16);m.store(at+2,248+n%8,16);m.store(at+4,256,16);m.store(at+6,1,16);m.store(at+8,n%256,16);
                setf(at+12,10.f+float(n%1000));setf(at+16,10.f+float(n%600));setf(at+20,248.f+float(n%8));setf(at+24,1.25f);setf(at+28,1.5f);setf(at+32,640);
            }
            base.fp=wr::FpState{};base.fp.put16(0,std::uint16_t(control));
        };
        const auto execute=[&](unsigned updates,bool optimized){
            std::optional<a::WeatherSourceReferenceScope> reference;if(!optimized)reference.emplace();
            const auto before=a::weather_batch_counts;
            for(unsigned update=0;update<updates;++update){p.cpu=base;p.cpu.eip=entry;m.store(stack,returning,32);
                for(unsigned boundaries=0;p.cpu.eip!=returning;++boundaries){CHECK(boundaries<65536);
                    a::LeafServiceScope leaf([](wr::Cpu&,wr::Memory&,void* owner){return static_cast<wr::Process*>(owner)->dispatch_leaf_api();},&p);
                    if(!(main_profile?main_program.invoke_unmetered(p.cpu):special_program.invoke(p.cpu)))CHECK(p.dispatch_api());
                }
            }
            unsigned sprites{};for(unsigned n=81;n<881;++n)sprites+=m.load(source.objects+n*256,8)!=0;
            return Result{p.cpu,snapshot(),a::weather_batch_counts.completed-before.completed,a::weather_batch_counts.yields-before.yields,sprites};
        };
        const auto check=[&](unsigned count,unsigned flags,unsigned vectors,unsigned control){
            prepare(count,flags,vectors,control);auto old=execute(2,false);
            prepare(count,flags,vectors,control);auto actual=execute(2,true);
            if(old.cpu.r!=actual.cpu.r || old.cpu.fp.bytes!=actual.cpu.fp.bytes || old.bytes!=actual.bytes){std::cerr<<"profile "<<main_profile<<" linear "<<linear<<" count "<<count<<" flags "<<flags<<" vectors "<<vectors<<" control "<<control<<" batches "<<actual.batches<<'\n';}
            CHECK(old.cpu.r==actual.cpu.r && old.cpu.eip==actual.cpu.eip && old.cpu.flags==actual.cpu.flags && old.cpu.defined_flags==actual.cpu.defined_flags);
            CHECK(old.cpu.fp.bytes==actual.cpu.fp.bytes && old.bytes==actual.bytes && !old.batches);
            CHECK(old.sprites==actual.sprites && actual.sprites>=count);
            CHECK(actual.batches>0);accepted+=actual.batches;prefixes+=actual.yields;
            if(main_profile)++main_cases;else ++special_cases;
        };
        for(unsigned count:{16u,64u,128u,400u})for(unsigned flags:{0u,0x800u})for(unsigned vectors:{0u,1u})check(count,flags,vectors,0x7f);
        for(unsigned control:{0x27fu,0x37fu,0x47fu,0x87fu,0xc7fu})check(16,0,1,control);
        if(!main_profile){
            // Compare real yields before source RNG/respawn calls against
            // untouched cached instruction C++, including the full FP state.
            for(unsigned reason=0;reason<3;++reason)for(unsigned vectors:{0u,1u})for(unsigned control:{0x7fu,0x37fu}){
                const auto reset=[&]{prepare(16,0x800,vectors,control);setf(source.weather+0x948+(reason?12:16),reason==1?-10000.f:10000.f);};
                reset();auto incoming=base;incoming.eip=source.begin;incoming.r[wr::ESP]=stack-44;incoming.r[wr::EBP]=stack-4;
                incoming.r[wr::EBX]=1;incoming.r[wr::ESI]=16;incoming.r[wr::EDI]=16;
                auto actual=incoming;const auto before=a::weather_batch_counts.yields;
                CHECK(s::update_weather(actual,m) && a::weather_batch_counts.yields==before+1);
                CHECK(actual.eip==0x408673 || actual.eip==0x4086cb || actual.eip==0x408723);
                live_prefixes+=actual.fp.bytes[4]!=0;
                const auto bytes=snapshot();reset();auto original=incoming;std::uint64_t budget=1000000;
                for(unsigned steps=0;original.eip!=actual.eip;++steps){CHECK(steps<100000 && source_oracle(original,m,budget));}
                CHECK(original.r==actual.r && original.flags==actual.flags && original.defined_flags==actual.defined_flags);
                CHECK(original.fp.bytes==actual.fp.bytes && bytes==snapshot());++prefixes;
            }
            std::array<wr::Cpu,8> resumed;unsigned resumed_index{};
            for(unsigned reason=0;reason<2;++reason)for(unsigned vectors:{0u,1u})for(unsigned control:{0x7fu,0x37fu}){
                const auto reset=[&]{prepare(16,0x800,vectors,control);const auto object=source.objects+81*256;
                    m.store(object,1,8);m.store(object+0x14,reason?4:1,8);m.store(object+0x10,reason?0x12345678:0,32);
                    m.store(stack-4-0x14,81,32);
                };
                reset();auto incoming=base;incoming.eip=source.resume;incoming.r[wr::ESP]=stack-44;incoming.r[wr::EBP]=stack-4;
                incoming.r[wr::EBX]=81;incoming.r[wr::ESI]=source.weather+0x960;incoming.r[wr::EDI]=0x800;
                // Independent fixtures must not share an update's eight-
                // prefix allowance just because a local variable is reused.
                auto& actual=resumed[resumed_index++];actual=incoming;const auto before=a::weather_batch_counts.yields;
                CHECK(s::update_weather(actual,m) && a::weather_batch_counts.yields==before+1);
                CHECK(actual.eip==(reason?0x43ac91u:0x43ac7cu));live_prefixes+=actual.fp.bytes[4]!=0;
                const auto bytes=snapshot();reset();auto original=incoming;std::uint64_t budget=1000000;
                for(unsigned steps=0;original.eip!=actual.eip;++steps){CHECK(steps<100000 && source_oracle(original,m,budget));}
                CHECK(original.r==actual.r && original.flags==actual.flags && original.defined_flags==actual.defined_flags);
                CHECK(original.fp.bytes==actual.fp.bytes && bytes==snapshot());++prefixes;
            }
            // Declining the transaction never writes any CPU or source RAM.
            prepare(16,0,1,0x7f);
            for(unsigned kind=0;kind<6;++kind){auto cpu=base;cpu.eip=source.begin;
                if(kind==0)cpu.eip=source.begin+1;if(kind==1)cpu.r[wr::ESP]=0;if(kind==2)cpu.flags|=wr::DF;
                if(kind==3)m.store(source.count,401,32);if(kind==4)cpu.fp.put16(0,0);
                const auto page=(source.objects+81*256)&~4095u;const auto permissions=m.permissions(page);
                if(kind==5)m.protect(page,4096,wr::Memory::Read);
                const auto before=cpu;const auto bytes=snapshot();CHECK(!s::update_weather(cpu,m));
                CHECK(before.r==cpu.r && before.eip==cpu.eip && before.fp.bytes==cpu.fp.bytes && before.flags==cpu.flags && before.defined_flags==cpu.defined_flags && bytes==snapshot());
                m.store(source.count,16,32);if(kind==5)m.protect(page,4096,permissions);++guards;
            }
        }
        CHECK(p.executed_instructions==0 && !p.gui() && !p.directdraw());
    }
    std::ofstream out(argv[1]);out<<"{\"passed\":true,\"main_source_cases\":"<<main_cases<<",\"special_source_cases\":"<<special_cases
        <<",\"accepted_batches\":"<<accepted<<",\"exact_prefix_yields\":"<<prefixes<<",\"unchanged_declines\":"<<guards
        <<",\"live_x87_prefix_cases\":"<<live_prefixes<<",\"source_oracle_component_only\":true"
        <<",\"full_cpu_fp_ram_equal\":true,\"maximum_particles\":400,\"game_launched\":false,\"screen_access\":false}\n";CHECK(out.good());
    std::cout<<"PASS Main/SC shared weather: "<<main_cases+special_cases<<" complete source cases\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
