// Differential test against the untouched one-instruction generated functions.
#include <winrecomp/compiled.hpp>
#include <winrecomp/core.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include "static_blocks.hpp"
#include "fp_sequences.hpp"
#include "block_flags.hpp"
bool code_page_1032(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1081(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1083(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1084(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1085(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1110(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1124(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1129(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1134(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1136(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1139(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1140(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1150(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1183(wr::Cpu&,wr::Memory&,std::uint64_t&);
using namespace wr;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
namespace {
constexpr U32 stop_pc=0xfffffff0,stack=0x07001000;
constexpr std::array<U32,14> code_pages={0x408000,0x439000,0x43b000,0x43c000,0x43d000,0x456000,0x464000,0x469000,0x46e000,0x470000,0x473000,0x474000,0x47e000,0x49f000};
const std::array<std::pair<U32,unsigned>,4> data_regions={{{0x4c2000,4096},{0xab3000,16384},{0x07000000,16384},{0x20000000,65536}}};
bool original(Cpu& cpu,Memory& memory,std::uint64_t& budget){
    switch(cpu.eip>>12){case 0x408:return code_page_1032(cpu,memory,budget);case 0x439:return code_page_1081(cpu,memory,budget);case 0x43b:return code_page_1083(cpu,memory,budget);case 0x43c:return code_page_1084(cpu,memory,budget);case 0x43d:return code_page_1085(cpu,memory,budget);case 0x456:return code_page_1110(cpu,memory,budget);case 0x464:return code_page_1124(cpu,memory,budget);case 0x469:return code_page_1129(cpu,memory,budget);case 0x46e:return code_page_1134(cpu,memory,budget);case 0x470:return code_page_1136(cpu,memory,budget);case 0x473:return code_page_1139(cpu,memory,budget);case 0x474:return code_page_1140(cpu,memory,budget);case 0x47e:return code_page_1150(cpu,memory,budget);case 0x49f:return code_page_1183(cpu,memory,budget);default:return false;}
}
std::set<U32> eligible;
std::array<std::uint8_t,0xa0000> eligible_bitmap{};
std::vector<std::uint8_t> original_image_bytes;
bool is_eligible(U32 pc){return pc>=0x400000 && pc<0x4a0000 && eligible_bitmap[pc-0x400000];}
struct Case {
    Cpu cpu;
    std::uint64_t budget{1000};
    unsigned slice{4096};
    U32 return_pc{stop_pc},corrupt{},readonly{},noexecute{};
    std::vector<std::pair<U32,U32>> words;
    bool writable_code{};
};
struct Result {
    Cpu cpu;
    std::uint64_t budget{},instructions{},batches{};
    std::vector<std::uint8_t> bytes;
    std::string fault;
};
struct Fixture {
    Memory memory;
    explicit Fixture(const Image& image,const Case& c){
        for(auto page:code_pages){memory.map(page,4096,Memory::Read|Memory::Execute|(c.writable_code?Memory::Write:0));memory.initialize(page,image.code(page).first(4096));}
        for(auto [at,size]:data_regions){
            memory.map(at,size,Memory::Read|Memory::Write);
            std::vector<std::uint8_t> bytes(size);
            for(unsigned n=0;n<size;++n)bytes[n]=std::uint8_t((n*17+(n>>3)+at/4096)&255);
            memory.initialize(at,bytes);
        }
        memory.map(0x4a2000,0x4000,Memory::Read);
        memory.initialize(0x4a2000,std::span(original_image_bytes).subspan(0x4a2000-image.base,0x4000));
        for(auto [at,value]:c.words)memory.store(at,value,32);
        if(c.corrupt)memory.initialize(c.corrupt,std::array<std::uint8_t,1>{0xcc});
        if(c.readonly)memory.protect(c.readonly&~4095u,4096,Memory::Read);
        if(c.noexecute)memory.protect(c.noexecute&~4095u,4096,Memory::Read);
    }
};
Result execute(const Image& image,const Case& c,bool fast){
    Fixture fixture(image,c);auto& memory=fixture.memory;auto cpu=c.cpu;auto budget=c.budget;Result result;
    try {
        auto scope=memory.execution_scope();unsigned count=0;
        while(count<c.slice && cpu.eip!=c.return_pc && is_eligible(cpu.eip)){
            if(fast)if(const auto n=wa2_block_batch(cpu,memory,budget,c.slice-count,c.return_pc)){
                CHECK(n<=c.slice-count);count+=n;result.instructions+=n;++result.batches;continue;
            }
            tick(budget,cpu.eip);CHECK(original(cpu,memory,budget));++count;++result.instructions;
        }
    } catch(const GuestFault& f){result.fault=std::to_string(int(f.kind))+":"+std::to_string(f.address);}
    result.cpu=cpu;result.budget=budget;
    auto append=[&](U32 at,unsigned size){const auto off=result.bytes.size();result.bytes.resize(off+size);memory.copy_out(at,std::span(result.bytes).subspan(off,size));};
    for(auto page:code_pages)append(page,4096);
    for(auto [at,size]:data_regions)append(at,size);
    return result;
}
void equal(const Result& a,const Result& b,U32 entry){
    if(a.cpu.r!=b.cpu.r || a.cpu.eip!=b.cpu.eip || a.cpu.flags!=b.cpu.flags || a.cpu.defined_flags!=b.cpu.defined_flags ||
       a.cpu.fp.bytes!=b.cpu.fp.bytes || a.cpu.selectors!=b.cpu.selectors ||
       a.cpu.fs_base!=b.cpu.fs_base || a.cpu.gs_base!=b.cpu.gs_base || a.cpu.fs_valid!=b.cpu.fs_valid || a.cpu.gs_valid!=b.cpu.gs_valid ||
       a.budget!=b.budget || a.instructions!=b.instructions || a.bytes!=b.bytes || a.fault!=b.fault){
        std::cerr<<"entry="<<std::hex<<entry<<" pc="<<a.cpu.eip<<"/"<<b.cpu.eip<<std::dec<<" instructions="<<a.instructions<<"/"<<b.instructions<<" budget="<<a.budget<<"/"<<b.budget<<" fault="<<a.fault<<"/"<<b.fault<<'\n';
        throw std::runtime_error("static block differs from generated oracle");
    }
}
Case color_case(std::mt19937& random){
    Case c;c.cpu.eip=0x43b290;c.cpu.r={random(),random(),random(),random(),stack,random(),random(),random()};
    c.cpu.flags=random();c.cpu.defined_flags=random();
    c.words={{stack,stop_pc},{stack+4,random()%16},{stack+8,random()%4}};
    for(unsigned off=12;off<=32;off+=4)c.words.emplace_back(stack+off,U32(std::int32_t(random()%1024)-256));
    return c;
}
Case vertex_case(std::mt19937& random){
    Case c;c.cpu.eip=0x43bde0;c.return_pc=0x43c100;c.cpu.r={random(),random(),random()%256,random(),stack,random(),0x20000200,(random()%4)*4};
    c.cpu.flags=random();c.cpu.defined_flags=random();
    c.words={{stack+0x48,0x20000100},{stack+0x10,random()%3},{stack+0x14,random()%2},
             {stack+0x18,random()%256},{stack+0x1c,random()%256},{stack+0x2c,c.cpu.r[EDX]},{stack+0x64,c.cpu.r[ESI]}};
    return c;
}
struct Benchmark {double seconds{};std::uint64_t instructions{},batches{};Cpu cpu;std::uint64_t budget{};std::vector<std::uint8_t> bytes;};
Benchmark benchmark(const Image& image,const Case& c,bool fast,unsigned repetitions){
    Fixture fixture(image,c);auto& memory=fixture.memory;Benchmark result;auto budget=1000000000ull;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned repeat=0;repeat<repetitions;++repeat){
        auto cpu=c.cpu;for(auto [at,value]:c.words)memory.store(at,value,32);
        auto scope=memory.execution_scope();unsigned slice=4096;
        while(slice && cpu.eip!=c.return_pc && is_eligible(cpu.eip)){
            if(fast)if(const auto n=wa2_block_batch(cpu,memory,budget,slice,c.return_pc)){slice-=n;result.instructions+=n;++result.batches;continue;}
            tick(budget,cpu.eip);CHECK(original(cpu,memory,budget));--slice;++result.instructions;
        }
        result.cpu=cpu;
    }
    result.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();result.budget=budget;
    for(auto [at,size]:data_regions){const auto off=result.bytes.size();result.bytes.resize(off+size);memory.copy_out(at,std::span(result.bytes).subspan(off,size));}
    return result;
}
}
int main(int argc,char** argv){try{
    CHECK(argc==3);const auto image=Image::load(argv[1]);original_image_bytes=image.mapped();std::mt19937 random(0x43b290);
    for(auto entry:wa2_block_entries()){eligible.insert(entry.pc);eligible_bitmap[entry.pc-0x400000]=1;}
    unsigned cases=0;std::uint64_t batches=0;
    // Every integer condition code must consume deferred ADD/SUB exactly,
    // including signed overflow, carry, narrow widths and retained flag bits.
    for(unsigned width:{8u,16u,32u})for(bool subtract:{false,true}){
        const auto sign=U32(1)<<(width-1),mask=wr::mask(width);
        const U32 values[]={0,1,mask,mask-1,sign,sign-1,sign+1,0x87654321,0xffffffff};
        for(auto left:values)for(auto right:values){
            Cpu expected;expected.flags=random();expected.defined_flags=random();auto actual=expected;
            if(subtract)wr::sub(expected,left,right,width);else wr::add(expected,left,right,width);
            {detail::BlockFlags pending(actual);pending.set(left,right,width,subtract);
                for(unsigned cc=0;cc<16;++cc)CHECK(pending.test(cc)==wr::test_condition(expected,cc));
            }
            CHECK(actual.flags==expected.flags && actual.defined_flags==expected.defined_flags);++cases;
        }
    }
    auto compare=[&](const Case& c){const auto a=execute(image,c,false),b=execute(image,c,true);equal(a,b,c.cpu.eip);++cases;batches+=b.batches;};
    // Every eligible entry is exercised with varied limits, flags, pointer/scalar
    // register values and unmapped addresses. This includes precise fault retry.
    for(auto entry:wa2_block_entries()){
        Case c;c.cpu.eip=entry.pc;c.cpu.flags=random();c.cpu.defined_flags=random();
        for(auto& reg:c.cpu.r){const auto choice=random()%3;reg=choice==0?random()%32:choice==1?0x20000100+random()%512:random();}
        c.cpu.r[ESP]=stack;c.budget=random()%36;c.slice=1+random()%48;
        for(auto& byte:c.cpu.fp.bytes)byte=std::uint8_t(random());
        c.cpu.fp.put16(0,0x37f);c.cpu.fp.put16(2,0);c.cpu.fp.put32(24,0x1f80);c.cpu.fp.bytes[4]=0;
        c.cpu.fs_base=random();c.cpu.gs_base=random();c.cpu.fs_valid=random()&1;c.cpu.gs_valid=random()&1;
        compare(c);
    }
    // Mixed integer/FP paths must retain precise state at every early return
    // and fall back for unmasked control words or failed operand preflight.
    for(const auto& sequence:wa2_fp_sequences())if(is_eligible(sequence.pc)){
        for(unsigned mode=0;mode<4;++mode){
            Case c;c.cpu.eip=sequence.pc;c.cpu.flags=random();c.cpu.defined_flags=random();
            for(unsigned r=0;r<8;++r)c.cpu.r[r]=0x20001000+r*0x400;c.cpu.r[ESP]=stack;
            c.slice=mode==0?1:mode==1?sequence.instructions:sequence.instructions+16;
            c.budget=mode==2?sequence.instructions-1:1000;
            if(mode==3)c.cpu.fp.put16(0,0x37e);
            compare(c);
        }
    }
    for(unsigned n=0;n<1000;++n){
        auto c=n&1?color_case(random):vertex_case(random);
        c.budget=n%3?1000:random()%120;c.slice=1+random()%160;
        if(n%17==0)c.readonly=n&1?0xab4000:stack;
        if(n%23==0)c.corrupt=c.cpu.eip+(n&1?3:4);
        if(n%29==0)c.noexecute=c.cpu.eip;
        compare(c);
    }
    // Stopping at every instruction inside the whole color function must leave
    // identical Cpu, memory and remaining budget, including zero-size slices.
    for(auto entry:wa2_block_entries())if(entry.pc>=0x43b290 && entry.pc<=0x43b3be){
        auto c=color_case(random);c.return_pc=entry.pc;compare(c);
        c.return_pc=stop_pc;c.slice=random()%4;c.budget=random()%4;compare(c);
    }
    // A store changes the next translated instruction. Its own guard must see
    // the write before execution, even within a native direct-control-flow run.
    {
        Case c;c.cpu.eip=0x43b3b2;c.cpu.r[ESP]=stack;c.cpu.r[EAX]=0xcccccccc;
        c.cpu.r[ESI]=U32(0x43b3b8-0xab401b);c.writable_code=true;compare(c);
    }
    CHECK(batches>0);
    auto colors=color_case(random);auto vertices=vertex_case(random);vertices.words[1].second=0;
    const auto a=benchmark(image,colors,false,100000),b=benchmark(image,colors,true,100000);
    // The jump into the color loop must yield before its first instruction,
    // allowing the process dispatcher to select the whole-loop kernel.
    {
        auto c=vertex_case(random);c.cpu.eip=0x43bdd7;c.cpu.r[EDI]=0;c.words[1].second=0;
        Fixture fixture(image,c);auto cpu=c.cpu;auto remaining=std::uint64_t(10000);
        auto scope=fixture.memory.execution_scope();
        const auto count=wa2_block_batch(cpu,fixture.memory,remaining,4096,stop_pc);
        CHECK(count==1 && cpu.eip==0x43bde0 && cpu.r[EDI]==0);
    }
    const auto v=benchmark(image,vertices,false,100000),w=benchmark(image,vertices,true,100000);
    auto same_benchmark=[](const Benchmark& x,const Benchmark& y){CHECK(x.instructions==y.instructions && x.budget==y.budget && x.bytes==y.bytes);CHECK(x.cpu.r==y.cpu.r && x.cpu.eip==y.cpu.eip && x.cpu.flags==y.cpu.flags && x.cpu.defined_flags==y.cpu.defined_flags && x.cpu.fp.bytes==y.cpu.fp.bytes);};
    same_benchmark(a,b);same_benchmark(v,w);
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.static-block-tests.v1\",\"cases\":"<<cases<<",\"entries\":"<<eligible.size()<<",\"exact_cpu_flags_fpu_memory_budget_faults_and_return_pc\":true,\"self_modified_code_checked\":true,\"color\":{\"instructions\":"<<a.instructions<<",\"before_seconds\":"<<a.seconds<<",\"after_seconds\":"<<b.seconds<<",\"speedup\":"<<a.seconds/b.seconds<<"},\"vertex\":{\"instructions\":"<<v.instructions<<",\"before_seconds\":"<<v.seconds<<",\"after_seconds\":"<<w.seconds<<",\"speedup\":"<<v.seconds/w.seconds<<"}}\n";CHECK(report);
    std::cout<<"PASS "<<cases<<" static block cases; color "<<a.seconds/b.seconds<<"x; vertex "<<v.seconds/w.seconds<<"x\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
