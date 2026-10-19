// Cached loader-loop replay in bounded, owned RAM. No resource, window,
// startup routine, device or game frame is executed.
#include "functions.hpp"
#include "image_colours.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace a=wa2::aot;
using wr::U32;
constexpr U32 Data=0x1200000,Tables=0x2200000,Stack=0x2500000,Partial=0x2600000;
struct Loop {U32 entry,exit;unsigned step,kind;};
constexpr Loop loops[]={{0x439156,0x439193,4,2},{0x4391de,0x439208,4,1},{0x43922a,0x439257,4,0},
                        {0x4392e6,0x439323,3,2},{0x43936e,0x439398,3,1},{0x4393ba,0x4393e7,3,0}};
bool same(const wr::Cpu& x,const wr::Cpu& y){
    return x.r==y.r && x.eip==y.eip && x.flags==y.flags && x.defined_flags==y.defined_flags && x.fp.bytes==y.fp.bytes &&
        x.selectors==y.selectors && x.fs_base==y.fs_base && x.gs_base==y.gs_base && x.fs_valid==y.fs_valid && x.gs_valid==y.gs_valid;
}
struct Result {wr::Cpu cpu;std::vector<std::uint8_t> image,tables,stack,partial;bool fault{};wr::FaultKind kind{};U32 address{};std::uint64_t epoch{};};
struct Fixture {
    wr::Memory memory;a::Program program{memory};std::vector<std::uint8_t> input,table_bytes=std::vector<std::uint8_t>(8192);
    wr::Cpu base;U32 start{},size{};unsigned counter{};
    explicit Fixture(bool linear){
        if(linear)CHECK(memory.enable_linear_memory());
        memory.map(0x401000,0xa0000,3);
        for(const auto& f:a::functions())for(const auto& span:f.code)memory.copy_in(span.address,span.bytes);
        memory.protect(0x401000,0xa0000,5);memory.map(0x4a1000,4096,3);memory.map(0xb33000,8192,3);
        memory.map(Data,9*1024*1024,3);memory.map(Tables,8192,3);memory.map(Stack,8192,3);memory.map(Partial,5001,3);
        memory.store(0x4a1074,0xf0001000,32);memory.store(0xb33ffc,Tables,32);
    }
    void reset(Loop loop,unsigned count,unsigned offset,unsigned seed){
        start=Data+64+offset;size=count*loop.step+offset+128;input.resize(size);
        for(unsigned n=0;n<input.size();++n)input[n]=std::uint8_t(n*31+(n>>8)*13+seed*41);
        for(unsigned n=0;n<table_bytes.size();++n)table_bytes[n]=std::uint8_t(n*(29+seed*2)+(n>>8)*37+seed);
        std::mt19937 random(seed+loop.entry);base=wr::Cpu{};
        for(auto& value:base.r)value=random();base.eip=loop.entry;
        base.flags=(random()&wr::STATUS_FLAGS)|0x602;base.defined_flags=random()&wr::STATUS_FLAGS;
        for(auto& byte:base.fp.bytes)byte=std::uint8_t(random());base.selectors={1,2,3,4,5,6};
        base.fs_base=0x12345678;base.gs_base=0xabcdef01;base.fs_valid=true;base.gs_valid=true;
        base.r[wr::EBX]=start;base.r[wr::ESP]=Stack+4096;
        const auto table=Tables+((seed&1)?4093:128);
        base.r[wr::ECX]=table;base.r[wr::EDI]=table+256;base.r[wr::ESI]=table+512;
        if(loop.kind==2){base.r[wr::EBP]=table+768;counter=wr::EDX;}
        else if(loop.kind==1)counter=wr::EBP;else counter=wr::ESI;
        base.r[counter]=count;
    }
    void prepare(){
        memory.protect(Data,9*1024*1024,3);memory.protect(Tables,8192,3);memory.protect(Partial,5001,3);
        memory.copy_in(Data,input);memory.copy_in(Tables,table_bytes);
        std::array<std::uint8_t,5001> partial;partial.fill(0x9d);memory.copy_in(Partial,partial);
        for(unsigned n=0;n<16;++n)memory.store(Stack+4096-32+n*4,0x98760000+n,32);
    }
    Result snapshot(wr::Cpu cpu){
        Result result;result.cpu=cpu;result.image.resize(size);result.tables.resize(8192);result.stack.resize(64);result.partial.resize(5001);
        memory.protect(Data,9*1024*1024,3);memory.protect(Tables,8192,3);
        memory.copy_out(Data,result.image);memory.copy_out(Tables,result.tables);memory.copy_out(Stack+4096-32,result.stack);memory.copy_out(Partial,result.partial);return result;
    }
    Result metered(Loop loop){
        auto cpu=base;std::uint64_t budget=std::uint64_t(base.r[counter])*(loop.kind==2?15:12)+(loop.kind==1?1:2);bool stopped{};
        try{CHECK(program.invoke(cpu,budget));}catch(const wr::GuestFault& error){stopped=error.kind==wr::FaultKind::budget && error.address==loop.exit;}
        CHECK(stopped && !budget && cpu.eip==loop.exit);return snapshot(cpu);
    }
    Result unmetered(bool native){
        auto cpu=base;std::optional<a::ImageColourReferenceScope> reference;if(!native)reference.emplace();
        const auto epoch=memory.code_epoch();bool fault{};wr::FaultKind kind{};U32 address{};
        try{CHECK(program.invoke_unmetered(cpu));}catch(const wr::GuestFault& error){fault=true;kind=error.kind;address=error.address;}
        const auto changes=memory.code_epoch()-epoch;auto result=snapshot(cpu);result.fault=fault;result.kind=kind;result.address=address;result.epoch=changes;return result;
    }
};
void equal(const Result& x,const Result& y){CHECK(same(x.cpu,y.cpu));CHECK(x.image==y.image && x.tables==y.tables && x.stack==y.stack && x.partial==y.partial);
    CHECK(x.fault==y.fault && x.kind==y.kind && x.address==y.address && x.epoch==y.epoch);}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned exact{},admitted{},integrated{},declines{},faults{},prefixes{},single_attempts{};
    for(bool linear:{false,true}){
        Fixture fixture(linear);
        for(auto loop:loops)for(unsigned count:{1u,2u,63u,64u,65u,255u,256u,257u,1025u})for(unsigned seed=0;seed<3;++seed){
            fixture.reset(loop,count,std::array{0u,4093u,4095u}[seed],seed);fixture.prepare();const auto old=fixture.metered(loop);
            fixture.prepare();auto cpu=fixture.base;const auto before=cpu;const bool accepted=a::native_image_colours(cpu,fixture.memory);
            CHECK(accepted==(count>=64));
            if(accepted){equal(old,fixture.snapshot(cpu));++admitted;}
            else{CHECK(same(cpu,before));equal(old,fixture.metered(loop));++declines;}
            ++exact;
        }
        for(auto loop:loops){
            fixture.reset(loop,1025,4093,1);fixture.prepare();const auto old=fixture.unmetered(false);
            fixture.prepare();const auto before=a::image_colour_counts();const auto now=fixture.unmetered(true);equal(old,now);
            CHECK(now.cpu.eip==0xf0001000 && !now.fault && a::image_colour_counts().completed==before.completed+1);++integrated;
            // Reject before mutation, then let the same static function retain
            // its exact partial-store prefix, fault PC and code epoch.
            for(unsigned reason=0;reason<6;++reason){
                fixture.reset(loop,1025,reason==0?4000:4093,reason);
                const auto alter=[&]{
                    fixture.prepare();
                    if(reason==0)fixture.memory.protect(Data+4096,4096,wr::Memory::Read);
                    if(reason==1)fixture.memory.protect(Data,9*1024*1024,wr::Memory::Write);
                    if(reason==2)fixture.memory.protect(Data,9*1024*1024,7);
                    if(reason==3)fixture.memory.protect(Tables+4096,4096,0);
                    if(reason==4)fixture.base.r[wr::ECX]=fixture.start;
                    if(reason==5)fixture.base.r[wr::EBX]=Partial+4990;
                };
                alter();auto cpu=fixture.base;const auto before=cpu;CHECK(!a::native_image_colours(cpu,fixture.memory));CHECK(same(cpu,before));
                auto unchanged=fixture.snapshot(cpu);fixture.prepare();auto original=fixture.snapshot(fixture.base);
                CHECK(unchanged.image==original.image && unchanged.tables==original.tables && unchanged.stack==original.stack && unchanged.partial==original.partial);
                alter();const auto reference=fixture.unmetered(false);alter();const auto attempts=a::image_colour_counts().attempts;
                const auto actual=fixture.unmetered(true);equal(reference,actual);
                CHECK(a::image_colour_counts().attempts==attempts+1);++single_attempts;
                const std::vector<std::uint8_t> untouched_partial(5001,0x9d);
                faults+=unsigned(actual.fault);prefixes+=unsigned(actual.fault && (actual.image!=fixture.input || actual.partial!=untouched_partial));++declines;
            }
            for(unsigned reason=0;reason<5;++reason){
                fixture.reset(loop,64,0,reason);fixture.prepare();auto cpu=fixture.base;
                if(reason==0)cpu.r[fixture.counter]=0;
                if(reason==1)cpu.r[fixture.counter]=0x80000000;
                if(reason==2)cpu.r[wr::EBX]=0;
                if(reason==3)cpu.r[wr::EBX]=0xfffffffe;
                if(reason==4)cpu.eip=0x12345678;
                const auto before=cpu;CHECK(!a::native_image_colours(cpu,fixture.memory));CHECK(same(cpu,before));++declines;
            }
        }
    }
    Fixture fixture(true);std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.shared-image-colours.v1\",\"exact_loop_cases\":"<<exact
        <<",\"admitted_cases\":"<<admitted<<",\"integrated_static_resumes\":"<<integrated<<",\"decline_cases\":"<<declines
        <<",\"precise_fault_cases\":"<<faults<<",\"partial_store_prefixes\":"<<prefixes<<",\"single_fallback_attempts\":"<<single_attempts<<",\"timings\":[";
    unsigned row{};
    for(auto index:{0u,1u,3u,4u}){
        const auto loop=loops[index];std::array<std::array<std::uint64_t,2>,5> timings{};
        for(unsigned pair=0;pair<5;++pair){std::array<Result,2> results;
            for(unsigned order=0;order<2;++order){const auto side=(pair+order)&1;
                fixture.reset(loop,1920*1080,1,pair);fixture.prepare();auto cpu=fixture.base;
                std::optional<a::ImageColourReferenceScope> reference;if(!side)reference.emplace();
                const auto begin=std::chrono::steady_clock::now();CHECK(fixture.program.invoke_unmetered(cpu));
                timings[pair][side]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
                results[side]=fixture.snapshot(cpu);
            }equal(results[0],results[1]);
        }
        if(row++)out<<',';out<<"{\"stride\":"<<loop.step<<",\"composed_tables\":"<<(loop.kind==2?"true":"false")<<",\"pixels\":2073600,\"paired_ns\":[";
        for(unsigned n=0;n<5;++n){if(n)out<<',';out<<'['<<timings[n][0]<<','<<timings[n][1]<<']';}out<<"]}";
    }
    out<<"],\"same_generated_static_resumes\":true,\"rgba_alpha_and_padding_exact\":true,\"mapped_and_linear_memory\":true,\"original_resources_opened\":false,\"game_launched\":false,\"screen_access\":false,\"device_fps_measured\":false,\"passed\":true}\n";
    std::cout<<"PASS shared image colour loops: "<<exact<<" exact cases, "<<integrated<<" compiled resumes, "<<faults<<" precise faults.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
