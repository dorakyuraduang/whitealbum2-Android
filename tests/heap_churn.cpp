#include <winrecomp/process.hpp>
#include "weak_registry.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
using namespace wr;
static bool no_code(Cpu&,Memory&,std::uint64_t&){throw std::runtime_error("unexpected guest instruction");}
static Image image(){Image x;x.base=0x400000;x.entry=0x401000;x.size=0x2000;x.headers_size=512;x.bytes.resize(1024);x.sections={{".text",0x1000,512,512,512,0x60000020}};x.bytes[512]=0xc3;return x;}
int main(int argc,char** argv){try{
    ProcessOptions options;options.instruction_budget=~std::uint64_t(0);Process p(no_code,options);
    CHECK(p.memory.enable_linear_memory());p.load(image());
    const auto baseline=p.memory.regions().size();
    const auto call=[&](const char* name,std::initializer_list<U32> args){return p.callback(p.resolve("kernel32.dll",name),std::span(args.begin(),args.size()));};
    const auto heap=call("HeapCreate",{0,0,0});CHECK(heap);
    constexpr unsigned count=32768;std::vector<U32> pointers(count);std::uint64_t requested=0;
    const auto started=std::chrono::steady_clock::now();
    for(unsigned n=0;n<count;++n){const U32 size=1+n%127;requested+=size;pointers[n]=call("HeapAlloc",{heap,8,size});CHECK(pointers[n] && !(pointers[n]&7));
        CHECK(p.memory.load(pointers[n],8)==0);p.memory.store(pointers[n],n%251,8);
        if(n==1023){std::uint64_t reserved=0;for(auto r:p.memory.regions())if(r.base>=0x10000000 && r.base<0x70000000)reserved+=r.size;
            CHECK(reserved<=1024*1024);}
    }
    std::uint64_t reserved=0;for(auto r:p.memory.regions())if(r.base>=0x10000000 && r.base<0x70000000)reserved+=r.size;
    CHECK(reserved<=requested*2+65536);
    std::vector<unsigned> order(count);std::iota(order.begin(),order.end(),0);std::mt19937 random(4242);std::shuffle(order.begin(),order.end(),random);
    for(auto n:order){CHECK(p.memory.load(pointers[n],8)==n%251);CHECK(call("HeapSize",{heap,0,pointers[n]})==1+n%127);CHECK(call("HeapFree",{heap,0,pointers[n]})==1);}
    // One empty pool is retained for subsequent temporary allocations.
    CHECK(p.memory.regions().size()==baseline+1);CHECK(call("HeapDestroy",{heap})==1);
    CHECK(p.memory.regions().size()==baseline);
    // Repeated reuse, neighboring blocks, in-place growth, failure atomicity,
    // and zeroing of newly exposed bytes must keep the Win32 heap contract.
    const auto h=call("HeapCreate",{0,0,4096});CHECK(h);
    auto a=call("HeapAlloc",{h,8,32}),b=call("HeapAlloc",{h,8,32});CHECK(a && b);p.memory.store(a,0x12345678,32);p.memory.store(b,0xaabbccdd,32);
    CHECK(call("HeapReAlloc",{h,0x10,a,4097})==0 && p.memory.load(a,32)==0x12345678 && p.memory.load(b,32)==0xaabbccdd);
    CHECK(call("HeapFree",{h,0,b})==1);CHECK(call("HeapReAlloc",{h,0x18,a,1024})==a);CHECK(p.memory.load(a,32)==0x12345678);
    for(unsigned n=32;n<1024;++n)CHECK(p.memory.load(a+n,8)==0);
    for(unsigned n=0;n<4000;++n){auto q=call("HeapAlloc",{h,8,127});CHECK(q);for(unsigned j=0;j<127;++j)CHECK(p.memory.load(q+j,8)==0);p.memory.store(q,0xdeadbeef,32);CHECK(call("HeapFree",{h,0,q})==1);}
    CHECK(call("HeapFree",{h,0,a})==1);CHECK(call("HeapFree",{h,0,a})==0);CHECK(call("HeapDestroy",{h})==1);CHECK(p.memory.regions().size()==baseline);
    const auto moving=call("HeapCreate",{0,0,4096});CHECK(moving);
    a=call("HeapAlloc",{moving,8,1024});b=call("HeapAlloc",{moving,8,1024});CHECK(a && b);
    p.memory.store(a,0x01234567,32);p.memory.store(b,0x89abcdef,32);
    const auto moved=call("HeapReAlloc",{moving,8,a,3072});CHECK(moved && moved!=a);
    CHECK(p.memory.load(moved,32)==0x01234567 && p.memory.load(b,32)==0x89abcdef);
    for(unsigned n=1024;n<3072;++n)CHECK(p.memory.load(moved+n,8)==0);
    CHECK(call("HeapFree",{moving,0,moved}) && call("HeapFree",{moving,0,b}));CHECK(call("HeapDestroy",{moving}));
    const auto large=call("HeapCreate",{0,0,0});CHECK(large);
    a=call("HeapAlloc",{large,8,16001});b=call("HeapAlloc",{large,8,32});CHECK(a && b);
    p.memory.store(a,0xaabbccdd,32);p.memory.store(a+15997,0x55667788,32);
    const auto grown=call("HeapReAlloc",{large,8,a,70000});CHECK(grown && grown!=a);
    CHECK(p.memory.load(grown,32)==0xaabbccdd && p.memory.load(grown+15997,32)==0x55667788 && p.memory.load(grown+69996,32)==0);
    CHECK(!call("HeapReAlloc",{large,0x18,grown,127000}) && p.memory.load(grown,32)==0xaabbccdd);
    CHECK(call("HeapSize",{large,0,grown})==70000);
    CHECK(call("HeapFree",{large,0,grown}) && call("HeapFree",{large,0,b}));CHECK(call("HeapDestroy",{large}));
    CHECK(p.memory.regions().size()==baseline);
    WeakRegistry<unsigned> registry;auto held=std::make_shared<unsigned>(99);registry.add(held);
    for(unsigned n=0;n<200000;++n)registry.add(std::make_shared<unsigned>(n));
    CHECK(registry.size()<=257);registry.collect();CHECK(registry.size()==1);held.reset();registry.collect();CHECK(!registry.size());
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    if(argc==2){std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.heap-churn.v1\",\"small_allocations\":"<<count<<",\"requested_bytes\":"<<requested<<",\"reserved_bytes_at_peak\":"<<reserved<<",\"reuse_cycles\":4000,\"weak_resource_cycles\":200000,\"regions_restored\":true,\"seconds\":"<<elapsed<<",\"screen_access\":false}\n";CHECK(out);}
    std::cout<<"PASS heap/resource churn: "<<count<<" concurrent small allocations, "<<reserved<<" reserved bytes for "<<requested<<" requested bytes; regions restored\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
