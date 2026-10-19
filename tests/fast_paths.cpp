// Compare the compiled batch against the original generated instruction bodies.
#include <winrecomp/compiled.hpp>
#include <winrecomp/core.hpp>
#include <winrecomp/hash.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <bit>
#include <cstring>
#include "fast_paths.hpp"
bool code_page_1079(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1090(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1124(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1150(wr::Cpu&,wr::Memory&,std::uint64_t&);
using namespace wr;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
struct Result {Cpu cpu;std::uint64_t budget{},instructions{},batches{};std::vector<std::uint8_t> bytes;std::string fault;double seconds{};};
struct Case {U32 eax{},esi{},edx{},ebp{},maximum{},flags{},defined{},destination{0x20000000},entry{0x4370c0};std::uint64_t budget{10000};unsigned slice{4096},size{8192};bool corrupt{},readonly{},linear{},executable_stack{},resource_ring{};std::vector<std::uint8_t> compressed;};
Result execute(const std::vector<std::uint8_t>& code,const Case& c,bool fast){
    const bool sort=c.entry==0x442300;const auto page=sort?0x442000u:0x437000u;
    Memory m;if(c.linear)CHECK(m.enable_linear_memory());m.map(page,4096,Memory::Read|Memory::Execute);m.initialize(page,code);
    m.map(0xa7c000,8192,Memory::Read|Memory::Write);m.map(0x07000000,16384,Memory::Read|Memory::Write|(c.executable_stack?Memory::Execute:0));
    if(sort)for(unsigned n=0;n<16384;++n)m.store(0x07000000+n,(n*17+(n>>4))&255,8);
    m.map(0x20000000,c.size,Memory::Read|Memory::Write);
    m.map(0x21000000,std::max<std::size_t>(4096,(c.compressed.size()+4095)&~std::size_t(4095)),Memory::Read|Memory::Write);m.copy_in(0x21000000,c.compressed);
    for(unsigned n=0;n<8192;++n)m.store(0xa7c000+n,(n*37+(n>>3))&255,8);
    if(c.resource_ring)for(unsigned n=0;n<4096;++n)m.store(0xa7cdd0+n,0,8);
    Cpu cpu;cpu.eip=c.entry;cpu.r={c.eax,0xbadf00d,c.edx,c.entry==0x437040?0x21000000u:0xcafe1234u,0x07000100,c.ebp,c.esi,c.destination};cpu.flags=c.flags;cpu.defined_flags=c.defined;
    m.store(cpu.r[ESP]+0x2c,c.maximum,32);m.store(cpu.r[ESP]+0x20,0x12345678,32);
    m.store(cpu.r[ESP]+0x28,U32(c.compressed.size()),32);
    if(c.corrupt)m.initialize(sort?0x442317:0x4370e8,std::array<std::uint8_t,1>{0xcc});
    if(c.readonly)m.protect(sort?0x07001000:0x20000000,4096,Memory::Read);
    Result result;auto budget=c.budget;const auto start=std::chrono::steady_clock::now();
    try{
        auto running=[&]{return cpu.eip>=(sort?0x442300u:c.entry==0x437040?0x437040u:0x4370c0u) && cpu.eip<(sort?0x44233cu:c.entry==0x437040?0x43710au:0x4370f1u);};
        while(running()){
            auto scope=m.execution_scope();unsigned count=0;
            while(count<c.slice && running()){
                if(fast)if(auto n=wa2_static_batch(cpu,m,budget,c.slice-count,0xffffffc0)){
                    CHECK(n<=c.slice-count);count+=n;result.instructions+=n;++result.batches;continue;
                }
                tick(budget,cpu.eip);CHECK(sort?code_page_1090(cpu,m,budget):code_page_1079(cpu,m,budget));++count;++result.instructions;
            }
        }
    }catch(const GuestFault& f){result.fault=std::to_string(int(f.kind))+":"+std::to_string(f.address);}
    result.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    result.cpu=cpu;result.budget=budget;
    for(auto [at,size]:{std::pair<U32,unsigned>{0xa7c000,8192},{0x07000000,16384},{0x20000000,c.size}}){
        const auto offset=result.bytes.size();result.bytes.resize(offset+size);m.copy_out(at,std::span(result.bytes).subspan(offset,size));
    }
    return result;
}
void equal(const Result& a,const Result& b){
    CHECK(a.cpu.r==b.cpu.r && a.cpu.eip==b.cpu.eip && a.cpu.flags==b.cpu.flags && a.cpu.defined_flags==b.cpu.defined_flags);
    CHECK(a.cpu.fp.bytes==b.cpu.fp.bytes && a.cpu.selectors==b.cpu.selectors);
    CHECK(a.cpu.fs_base==b.cpu.fs_base && a.cpu.gs_base==b.cpu.gs_base && a.cpu.fs_valid==b.cpu.fs_valid && a.cpu.gs_valid==b.cpu.gs_valid);
    CHECK(a.budget==b.budget && a.instructions==b.instructions && a.bytes==b.bytes && a.fault==b.fault);
}
struct LoopCase {
    bool fill{true},writable_code{};U32 source{0x21000000},destination{0x20000000},count{4},sp{0x07000100},flags{0x202},defined{STATUS_FLAGS},fp_seed{},mxcsr{0x1f80},return_pc{0xffffffc0};
    U32 protect_address{};unsigned permissions{Memory::Read},slice{4096},size{8192};int corrupt_offset{-1};std::uint64_t budget{10000};
};
Result execute_loop(const std::vector<std::uint8_t>& code,const LoopCase& c,bool fast){
    const U32 begin=c.fill?0x47e798:0x464aa1,end=c.fill?0x47e7c8:0x464ab4,page=begin&~4095u;
    Memory m;m.map(page,4096,Memory::Read|Memory::Execute|(c.writable_code?Memory::Write:0));m.initialize(page,code);
    const std::array<std::pair<U32,unsigned>,4> data={{{0x07000000,4096},{0x20000000,c.size},{0x21000000,c.size},{0xfffff000,4096}}};
    for(auto [base,size]:data){m.map(base,size,Memory::Read|Memory::Write);std::vector<std::uint8_t> bytes(size);for(unsigned i=0;i<size;++i)bytes[i]=std::uint8_t(i*37+(i>>4)+(base>>24));m.copy_in(base,bytes);}
    Cpu cpu;cpu.eip=begin;cpu.r={c.destination,c.fill?c.count:c.source-c.destination,0x12345678,0xabcdef01,c.sp,0x76543210,0x31415926,c.destination};cpu.flags=c.flags;cpu.defined_flags=c.defined;
    cpu.fs_base=0x72112200;cpu.gs_base=0x8aa11200;cpu.fs_valid=true;cpu.gs_valid=true;
    std::mt19937 random(c.fp_seed);for(unsigned i=32;i<cpu.fp.bytes.size();++i)cpu.fp.bytes[i]=std::uint8_t(random());cpu.fp.put32(24,c.mxcsr);
    if(!c.fill){try{m.store(c.sp+0x2c,c.count,32);}catch(const GuestFault&){} }
    if(c.corrupt_offset>=0)m.initialize(begin+U32(c.corrupt_offset),std::array<std::uint8_t,1>{0xcc});
    if(c.protect_address)m.protect(c.protect_address,4096,c.permissions);
    Result result;auto budget=c.budget;const auto start=std::chrono::steady_clock::now();
    try{
        const auto running=[&]{return cpu.eip>=begin && cpu.eip<end && cpu.eip!=c.return_pc;};
        while(running()){
            auto scope=m.execution_scope();unsigned count=0;
            while(count<c.slice && running()){
                if(fast)if(auto n=wa2_static_batch(cpu,m,budget,c.slice-count,c.return_pc)){CHECK(n<=c.slice-count);count+=n;result.instructions+=n;++result.batches;continue;}
                tick(budget,cpu.eip);CHECK(c.fill?code_page_1150(cpu,m,budget):code_page_1124(cpu,m,budget));++count;++result.instructions;
            }
        }
    }catch(const GuestFault& f){result.fault=std::to_string(int(f.kind))+":"+std::to_string(f.address);}
    result.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();result.cpu=cpu;result.budget=budget;
    for(auto [base,size]:data){m.protect(base,size,Memory::Read);const auto offset=result.bytes.size();result.bytes.resize(offset+size);m.copy_out(base,std::span(result.bytes).subspan(offset,size));}
    m.protect(page,4096,Memory::Read);const auto offset=result.bytes.size();result.bytes.resize(offset+4096);m.copy_out(page,std::span(result.bytes).subspan(offset,4096));return result;
}
#include "sort_objects.hpp"
int main(int argc,char** argv){try{
    CHECK(argc==3 || argc==4);const auto image=Image::load(argv[1]);const auto page=image.code(0x437000).first(4096);
    const std::vector<std::uint8_t> code(page.begin(),page.end());
    std::mt19937 random(0x4370c0);unsigned cases=sort_object_test::run(image);std::uint64_t batches=0;
    for(unsigned n=0;n<1000;++n){
        Case c;c.eax=random();c.esi=random();c.edx=random()%40;c.ebp=random()%24;c.maximum=random()%40;c.flags=random();c.defined=random();c.slice=1+random()%4096;c.destination+=random()%8100;
        c.budget=n%3?10000:random()%700;c.corrupt=n%29==0;c.readonly=n%31==0;
        const auto a=execute(code,c,false),b=execute(code,c,true);equal(a,b);batches+=b.batches;++cases;
    }
    for(auto edx:{0x7fffffffu,0x80000000u,0x80000001u,0xffffffffu})for(auto ebp:{0u,0x7fffffffu,0x80000000u,0xffffffffu}){
        Case c;c.edx=edx;c.ebp=ebp;c.maximum=0x7fffffffu;c.budget=700;c.flags=0xffffffffu;
        equal(execute(code,c,false),execute(code,c,true));++cases;
    }
    for(auto destination:{0xa7cdd0u,0x07000120u,0x20001fffu}){
        Case c;c.eax=0xfff;c.esi=0xffe;c.edx=17;c.maximum=100;c.destination=destination;
        equal(execute(code,c,false),execute(code,c,true));++cases;
    }
    CHECK(batches>0);
    for(unsigned n=0;n<1000;++n){
        Case c;c.entry=0x437040;c.linear=n&1;c.esi=random();c.flags=random();c.defined=random();c.maximum=0;
        for(unsigned group=0;group<1+random()%10;++group){
            const auto flags=std::uint8_t(random());c.compressed.push_back(flags);
            for(unsigned bit=0;bit<8;++bit){c.compressed.push_back(std::uint8_t(random()));if(flags&(1u<<bit))++c.maximum;
                else{const auto high=std::uint8_t(random());c.compressed.push_back(high);c.maximum+=(high&15)+3;}}
        }
        c.slice=1+random()%4096;c.budget=n%3?100000:random()%1500;c.corrupt=n%29==0;c.readonly=n%31==0;
        if(n%7==0)c.maximum/=2;if(n%11==0)c.compressed.pop_back();
        const auto a=execute(code,c,false),b=execute(code,c,true);equal(a,b);batches+=b.batches;++cases;
    }
    Case benchmark;benchmark.eax=0x123;benchmark.esi=0xfee;benchmark.edx=1999999;benchmark.maximum=2000000;benchmark.size=2000016;benchmark.budget=34000001;
    const auto before=execute(code,benchmark,false),after=execute(code,benchmark,true);equal(before,after);
    CHECK(after.instructions==34000000 && after.batches>0);
    Case compressed;compressed.entry=0x437040;compressed.esi=0xfee;compressed.budget=200000000;compressed.slice=32768;
    for(unsigned n=0;n<8192;++n){compressed.compressed.push_back(0xff);for(unsigned k=0;k<8;++k)compressed.compressed.push_back(std::uint8_t(n*17+k));compressed.maximum+=8;}
    for(unsigned n=0;n<8192;++n){compressed.compressed.push_back(0);for(unsigned k=0;k<8;++k){compressed.compressed.push_back(0);compressed.compressed.push_back(15);}compressed.maximum+=144;}
    compressed.size=compressed.maximum+4096;
    const auto resource_before=execute(code,compressed,true);compressed.linear=true;
    const auto resource_after=execute(code,compressed,true);equal(resource_before,resource_after);
    CHECK(resource_after.batches>0 && resource_after.bytes==resource_before.bytes);
    struct ResourceSample {std::string name,sha;U32 bytes;double before,after;};std::vector<ResourceSample> resource_samples;
    if(argc==4){
        std::ifstream input(argv[3],std::ios::binary);std::array<char,8> magic{};input.read(magic.data(),magic.size());
        CHECK((magic==std::array<char,8>{'W','2','R','S','v','1',0,0}));
        const auto read32=[&]{U32 value{};input.read(reinterpret_cast<char*>(&value),4);CHECK(input);return value;};
        const auto count=read32();CHECK(count>0 && count<=16);
        for(U32 n=0;n<count;++n){
            const auto name_size=read32(),source_size=read32(),decoded_size=read32();CHECK(name_size<=128 && source_size<=32*1024*1024 && decoded_size<=32*1024*1024);
            std::string name(name_size,'\0'),expected(64,'\0');input.read(name.data(),name.size());input.read(expected.data(),expected.size());
            Case resource;resource.entry=0x437040;resource.esi=0xfee;resource.budget=4000000000ull;resource.slice=32768;resource.resource_ring=true;
            resource.maximum=decoded_size;resource.size=(decoded_size+8191)&~4095u;resource.compressed.resize(source_size);
            input.read(reinterpret_cast<char*>(resource.compressed.data()),source_size);CHECK(input);
            const auto old=execute(code,resource,true);resource.linear=true;const auto now=execute(code,resource,true);equal(old,now);
            CHECK(now.fault.empty() && now.cpu.r[wr::EBP]==decoded_size);
            const auto actual=sha256(std::span(now.bytes).subspan(8192+16384,decoded_size));CHECK(actual==expected);
            resource_samples.push_back({name,actual,decoded_size,old.seconds,now.seconds});
            std::cout<<"PASS resource "<<name<<" bytes="<<decoded_size<<" decode "<<old.seconds<<" -> "<<now.seconds<<" s; reference SHA256 matches\n";
        }
    }
    const auto sort_page=image.code(0x442000).first(4096);const std::vector<std::uint8_t> sort_code(sort_page.begin(),sort_page.end());
    for(unsigned n=0;n<1000;++n){Case c;c.entry=0x442300;c.linear=n&1;c.eax=random();c.edx=random();c.esi=random()%2050;c.destination=random()%2050;c.flags=random();c.defined=random();c.slice=1+random()%4096;c.budget=n%3?100000:random()%1000;c.corrupt=n%29==0;c.readonly=n%31==0;equal(execute(sort_code,c,false),execute(sort_code,c,true));++cases;}
    for(auto slice:{1u,8u,9u,10u,18u,4096u})for(auto budget:{0u,1u,8u,9u,17u,100000u})for(unsigned mode=0;mode<5;++mode){
        Case c;c.entry=0x442300;c.linear=true;c.esi=2046;c.destination=mode&1?2040:0;c.slice=slice;c.budget=budget;
        c.eax=random();c.edx=random();c.flags=random();c.defined=random();c.readonly=mode==2;c.corrupt=mode==3;c.executable_stack=mode==4;
        equal(execute(sort_code,c,false),execute(sort_code,c,true));++cases;
    }
    std::array<Result,2> loop_before,loop_after;std::array<std::uint64_t,2> loop_batches{};
    for(unsigned kind=0;kind<2;++kind){
        const bool fill=kind==0;const auto region=image.code(fill?0x47e000:0x464000).first(4096);const std::vector<std::uint8_t> loop_code(region.begin(),region.end());
        const auto verify=[&](const LoopCase& c){const auto a=execute_loop(loop_code,c,false),b=execute_loop(loop_code,c,true);try{equal(a,b);}catch(...){std::cerr<<"Loop mismatch kind="<<kind<<" case="<<cases<<" count="<<c.count<<" slice="<<c.slice<<" budget="<<c.budget<<" source="<<std::hex<<c.source<<" destination="<<c.destination<<std::dec<<'\n';throw;}loop_batches[kind]+=b.batches;++cases;};
        for(unsigned n=0;n<1000;++n){
            LoopCase c;c.fill=fill;c.count=random()%80;c.flags=random();c.defined=random();c.fp_seed=random();c.mxcsr=(random()&0xffffu);c.slice=1+random()%4096;c.budget=n%3?10000:random()%1000;
            c.destination+=fill?(random()%500)*16:random()%8000;c.source+=random()%8000;
            if(n%29==0)c.corrupt_offset=int(random()%(fill?48:19));if(n%31==0)c.protect_address=0x20001000;
            if(fill && n%17==0)++c.destination;if(n%41==0)c.mxcsr|=0x10000;verify(c);
        }
        // Each instruction boundary, partial budget, scheduler slice and return
        // PC must retain the original dispatch/fault state.
        for(unsigned n=0;n<40;++n){LoopCase c;c.fill=fill;c.count=7;c.budget=n;verify(c);c.budget=1000;c.slice=n+1;verify(c);}
        for(auto count:{0u,1u,0x7fffffffu,0x80000000u,0xffffffffu}){LoopCase c;c.fill=fill;c.count=count;c.budget=300;verify(c);}
        for(auto destination:{0x20001ff0u,0xfffffff0u,0xffffff80u,0x07000120u,0x0700012cu,fill?0x47e790u:0x464aa2u}){LoopCase c;c.fill=fill;c.destination=destination;c.writable_code=true;verify(c);}
        for(auto address:{0x07000000u,0x20000000u,0x21000000u}){LoopCase c;c.fill=fill;c.protect_address=address;c.permissions=Memory::Read;verify(c);c.permissions=Memory::Write;verify(c);}
        {LoopCase c;c.fill=fill;c.return_pc=fill?0x47e7a6:0x464aaa;verify(c);}
        if(!fill){
            for(auto source:{0x20000000u,0x20000004u,0x1ffffffcu,0x0700012cu,0xfffffffcu}){LoopCase c;c.fill=false;c.source=source;verify(c);}
            for(auto sp:{0xffffffd0u,0xfffffff0u,0x464a75u}){LoopCase c;c.fill=false;c.sp=sp;c.writable_code=true;verify(c);}
            LoopCase c;c.fill=false;c.source=0x20000000;c.destination=0x20000004;verify(c);
        }
        CHECK(loop_batches[kind]>0);
        LoopCase c;c.fill=fill;c.count=fill?16384:262144;c.size=fill?c.count*128:c.count*4;c.budget=std::uint64_t(c.count)*(fill?11:7);c.fp_seed=0x12345678;
        loop_before[kind]=execute_loop(loop_code,c,false);loop_after[kind]=execute_loop(loop_code,c,true);equal(loop_before[kind],loop_after[kind]);CHECK(loop_after[kind].batches>0 && loop_after[kind].fault.empty());++cases;
    }
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.static-batch-tests.v2\",\"cases\":"<<cases<<",\"exact_cpu_flags_fpu_memory_budget_and_faults\":true,\"guest_instructions\":"<<after.instructions<<",\"before_seconds\":"<<before.seconds<<",\"after_seconds\":"<<after.seconds<<",\"speedup\":"<<before.seconds/after.seconds<<",\"batch_count\":"<<after.batches<<",\"memory_sha256\":\""<<sha256(after.bytes)<<"\",\"steady_loops\":[";
    for(unsigned kind=0;kind<2;++kind){if(kind)report<<',';report<<"{\"entry\":"<<(kind==0?0x47e798:0x464aa1)<<",\"guest_instructions\":"<<loop_after[kind].instructions<<",\"before_seconds\":"<<loop_before[kind].seconds<<",\"after_seconds\":"<<loop_after[kind].seconds<<",\"batches\":"<<loop_after[kind].batches<<",\"memory_sha256\":\""<<sha256(loop_after[kind].bytes)<<"\"}";}
    report<<"],\"resource_decode\":{\"decoded_bytes\":"<<compressed.maximum<<",\"checked_memory_seconds\":"<<resource_before.seconds<<",\"linear_seconds\":"<<resource_after.seconds<<",\"same_cpu_flags_memory_budget\":true,\"memory_sha256\":\""<<sha256(resource_after.bytes)<<"\"},\"actual_resources\":[";
    for(unsigned n=0;n<resource_samples.size();++n){if(n)report<<',';const auto& r=resource_samples[n];report<<"{\"member\":\""<<r.name<<"\",\"decoded_bytes\":"<<r.bytes<<",\"checked_seconds\":"<<r.before<<",\"linear_seconds\":"<<r.after<<",\"sha256\":\""<<r.sha<<"\"}";}report<<"]}\n";
    CHECK(report);std::cout<<"PASS "<<cases<<" static-batch equivalence cases; fixed loop "<<before.seconds<<" -> "<<after.seconds<<" seconds ("<<before.seconds/after.seconds<<"x)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
