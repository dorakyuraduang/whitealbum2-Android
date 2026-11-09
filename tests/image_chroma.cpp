// Compare derived hooks with the exact untouched cached C++ bodies, in own RAM.
#include "functions.hpp"
#include "image_chroma.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
namespace wa2::aot::chroma_reference {void invoke(wr::Cpu&,wr::Memory&);void step(wr::Cpu&,wr::Memory&,std::uint64_t);}
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check at ")+std::to_string(__LINE__)+": "+#x);}while(false)
namespace a=wa2::aot;using wr::U32;
constexpr U32 Data=0x1200000,Stack=0x2500000,Partial=0x2600000,Luts=0xa83000,Return=0xf0001234;
bool same(const wr::Cpu& x,const wr::Cpu& y){return x.r==y.r && x.eip==y.eip && x.flags==y.flags && x.defined_flags==y.defined_flags && x.fp.bytes==y.fp.bytes && x.selectors==y.selectors && x.fs_base==y.fs_base && x.gs_base==y.gs_base && x.fs_valid==y.fs_valid && x.gs_valid==y.gs_valid;}
struct Result {wr::Cpu cpu;std::vector<std::uint8_t> data,luts,stack,partial;bool fault{};wr::FaultKind kind{};U32 address{};std::uint64_t epoch{},ns{};};
struct Fixture {
    wr::Memory memory;a::Program program{memory};wr::Cpu cpu;std::vector<std::uint8_t> input,luts=std::vector<std::uint8_t>(0x12000),stack=std::vector<std::uint8_t>(128),partial=std::vector<std::uint8_t>(5001);U32 pixels{},entry{},chroma{},order{},start{};unsigned step{};
    explicit Fixture(bool linear){if(linear)CHECK(memory.enable_linear_memory());memory.map(0x401000,0xa0000,3);
        for(const auto& f:a::functions())for(const auto& span:f.code)memory.copy_in(span.address,span.bytes);
        memory.protect(0x401000,0xa0000,5);memory.map(Data,6u*1024*1024,3);memory.map(Stack,8192,3);memory.map(Partial,8192,3);memory.map(Luts,0x12000,3);
        auto scope=memory.execution_scope();CHECK(bool(scope.linear_base())==linear);
    }
    void reset(unsigned format,U32 count,U32 saturation,unsigned offset,unsigned seed){step=format==2?3:4;entry=step==3?0x438a00:0x438820;order=format==1;pixels=count;chroma=saturation;start=Data+64+offset;
        input.resize(128+offset+std::size_t(count)*step);std::mt19937 random(seed+format*123+count);cpu=wr::Cpu{};for(auto& reg:cpu.r)reg=random();cpu.eip=entry;cpu.r[wr::ESP]=Stack+4096;
        cpu.flags=(random()&wr::STATUS_FLAGS)|0x602;cpu.defined_flags=random()&wr::STATUS_FLAGS;for(auto& byte:cpu.fp.bytes)byte=std::uint8_t(random());cpu.selectors={1,2,3,4,5,6};cpu.fs_base=0x12345678;cpu.gs_base=0x87654321;cpu.fs_valid=cpu.gs_valid=true;
        for(unsigned n=0;n<input.size();++n)input[n]=std::uint8_t(n*31+(n>>8)*17+seed*71);
        for(unsigned n=0;n<luts.size();++n)luts[n]=std::uint8_t(n*37+(n>>8)*43+seed*29);
        for(unsigned n=0;n<stack.size();++n)stack[n]=std::uint8_t(n*11+seed);for(unsigned n=0;n<partial.size();++n)partial[n]=std::uint8_t(n*13+seed);
    }
    void prepare(){memory.copy_in(Data,input);memory.copy_in(Luts,luts);memory.copy_in(Stack+4096-64,stack);memory.copy_in(Partial,partial);
        const auto sp=cpu.r[wr::ESP];memory.store(sp,Return,32);memory.store(sp+4,start,32);memory.store(sp+8,pixels,32);memory.store(sp+12,chroma,32);memory.store(sp+16,order,32);
    }
    Result run(bool native){auto context=cpu;const auto epoch=memory.code_epoch();const auto time=std::chrono::steady_clock::now();Result result;
        try{if(native)CHECK(program.invoke_unmetered(context));else a::chroma_reference::invoke(context,memory);}catch(const wr::GuestFault& error){result.fault=true;result.kind=error.kind;result.address=error.address;}
        result.ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-time).count();result.cpu=context;result.epoch=memory.code_epoch()-epoch;return result;
    }
    void snapshot(Result& result){result.data.resize(input.size());result.luts.resize(luts.size());result.stack.resize(stack.size());result.partial.resize(partial.size());memory.copy_out(Data,result.data);memory.copy_out(Luts,result.luts);memory.copy_out(Stack+4096-64,result.stack);memory.copy_out(Partial,result.partial);}
};
void equal(const Result& x,const Result& y){CHECK(same(x.cpu,y.cpu));CHECK(x.data==y.data && x.luts==y.luts && x.stack==y.stack && x.partial==y.partial);CHECK(x.fault==y.fault && x.kind==y.kind && x.address==y.address && x.epoch==y.epoch);}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned exact{},faults{},declines{},tail_cases{},admissions{},loop_states{};std::array<std::array<std::array<std::array<std::uint64_t,2>,5>,3>,2> timings{};
    for(unsigned mode=0;mode<2;++mode){Fixture fixture{bool(mode)};
        for(unsigned format=0;format<3;++format)for(unsigned count:{0u,1u,63u,64u,65u,257u,1025u})for(unsigned chroma:{0u,1u,17u,127u,255u,256u})for(unsigned seed=0;seed<3;++seed){
            fixture.reset(format,count,chroma,std::array{0u,4093u,4095u}[seed],seed);fixture.prepare();auto before=fixture.run(false);fixture.snapshot(before);
            fixture.prepare();const auto admitted=a::image_chroma_counts().completed;auto after=fixture.run(true);fixture.snapshot(after);equal(before,after);
            CHECK(!after.fault && after.cpu.eip==Return && after.cpu.r[wr::EAX]==1);CHECK(a::image_chroma_counts().completed-admitted==unsigned(count>=64 && chroma!=256));++exact;admissions+=unsigned(count>=64 && chroma!=256);
        }
        for(unsigned format=0;format<3;++format)for(unsigned chroma:{0u,127u})for(unsigned count:{64u,65u,257u}){
            fixture.reset(format,count,chroma,4093,37);const unsigned prefix=fixture.step==3?(chroma?15:11):(chroma?19:13);
            const unsigned loop_prefix=fixture.step==3?(chroma?5:3):(chroma?4:format==1?2:3);
            const U32 expected_exit=fixture.step==3?(chroma?0x438af1:0x438a65):chroma?(format==1?0x438971:0x4389f1):(format==1?0x438889:0x4388d5);
            fixture.prepare();Result old;old.cpu=fixture.cpu;a::chroma_reference::step(old.cpu,fixture.memory,prefix);const auto start=old.cpu.eip;
            a::chroma_reference::step(old.cpu,fixture.memory,std::uint64_t(count)*(chroma?26:16)+loop_prefix);CHECK(old.cpu.eip==expected_exit);fixture.snapshot(old);
            fixture.prepare();Result now;now.cpu=fixture.cpu;a::chroma_reference::step(now.cpu,fixture.memory,prefix);CHECK(now.cpu.eip==start);
            CHECK(a::native_image_chroma(now.cpu,fixture.memory));fixture.snapshot(now);equal(old,now);++loop_states;
        }
        fixture.memory.release(Partial);fixture.memory.map(Partial,5001,3);
        for(unsigned format=0;format<3;++format)for(unsigned chroma:{0u,127u})for(unsigned reason=0;reason<6;++reason){
            fixture.reset(format,1025,chroma,4093,reason);
            const auto alter=[&]{fixture.prepare();if(reason==0){fixture.memory.store(fixture.cpu.r[wr::ESP]+4,Data+4000,32);fixture.memory.protect(Data+4096,4096,wr::Memory::Read);}if(reason==1)fixture.memory.protect(Data,3*4096,wr::Memory::Write);if(reason==2)fixture.memory.protect(Data,3*4096,7);
                if(reason==3)fixture.memory.protect(0xa8b000,4096,0);if(reason==4)fixture.memory.store(fixture.cpu.r[wr::ESP]+4,0xa83db0+chroma*257,32);if(reason==5)fixture.memory.store(fixture.cpu.r[wr::ESP]+4,Partial+4990,32);};
            const auto restore=[&]{fixture.memory.protect(Data,3*4096,3);fixture.memory.protect(0xa8b000,4096,3);};
            alter();auto before=fixture.run(false);restore();fixture.snapshot(before);alter();const auto count=a::image_chroma_counts().completed;auto after=fixture.run(true);restore();fixture.snapshot(after);equal(before,after);CHECK(a::image_chroma_counts().completed-count==unsigned(!chroma && (reason==3 || reason==4)));faults+=after.fault;++declines;
        }
        for(unsigned format=0;format<3;++format){fixture.reset(format,64,127,0,9);fixture.prepare();fixture.memory.store(fixture.cpu.r[wr::ESP]+4,Partial+5001-(64*fixture.step-(fixture.step-3)),32);
            auto before=fixture.run(false);fixture.snapshot(before);fixture.prepare();fixture.memory.store(fixture.cpu.r[wr::ESP]+4,Partial+5001-(64*fixture.step-(fixture.step-3)),32);auto after=fixture.run(true);fixture.snapshot(after);equal(before,after);CHECK(!after.fault);++tail_cases;
            for(unsigned reason=0;reason<3;++reason){fixture.reset(format,64,reason==2?0xffffffffu:127u,0,reason);fixture.prepare();if(reason==0)fixture.memory.store(fixture.cpu.r[wr::ESP]+8,0x80000000,32);if(reason==1)fixture.memory.store(fixture.cpu.r[wr::ESP]+4,0xfffffff0,32);
                auto old=fixture.run(false);fixture.snapshot(old);fixture.prepare();if(reason==0)fixture.memory.store(fixture.cpu.r[wr::ESP]+8,0x80000000,32);if(reason==1)fixture.memory.store(fixture.cpu.r[wr::ESP]+4,0xfffffff0,32);auto now=fixture.run(true);fixture.snapshot(now);equal(old,now);++declines;
            }
        }
        fixture.memory.release(Partial);fixture.memory.map(Partial,8192,3);
        for(unsigned kind=0;kind<3;++kind){fixture.reset(kind==2?2:1,262144,kind==0?0:127,0,71);fixture.prepare();fixture.run(true);
            for(unsigned round=0;round<5;++round){Result results[2];for(unsigned order=0;order<2;++order){const auto side=(round+order)&1;fixture.prepare();results[side]=fixture.run(bool(side));timings[mode][kind][round][side]=results[side].ns;fixture.snapshot(results[side]);}equal(results[0],results[1]);}
        }
    }
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.image-chroma.v1\",\"exact_cpu_flags_stack_and_pixel_cases\":"<<exact<<",\"admitted_cases\":"<<admissions<<",\"loop_exit_state_cases\":"<<loop_states<<",\"decline_cases\":"<<declines<<",\"precise_fault_cases\":"<<faults<<",\"partial_region_tail_cases\":"<<tail_cases<<",\"timings\":[";
    for(unsigned mode=0;mode<2;++mode)for(unsigned kind=0;kind<3;++kind){if(mode || kind)out<<',';out<<"{\"linear_memory\":"<<(mode?"true":"false")<<",\"kind\":"<<kind<<",\"pixels\":262144,\"paired_ns\":[";for(unsigned round=0;round<5;++round){if(round)out<<',';out<<'['<<timings[mode][kind][round][0]<<','<<timings[mode][kind][round][1]<<']';}out<<"]}";}
    out<<"],\"direct_linear_path_exercised\":true,\"cached_reference_bodies_unchanged\":true,\"original_resources_opened\":false,\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out.good());std::cout<<"PASS image chroma: "<<exact<<" exact source comparisons, "<<admissions<<" native loops.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
