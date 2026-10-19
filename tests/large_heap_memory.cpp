// Owned RAM only: large resource-buffer reuse, permissions and heap contracts.
#include "process.hpp"
#include <winrecomp/win32_state.hpp>
#include <winrecomp/heap_cache.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
using namespace wr;
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned checks{},reuse_cycles{};
    for(bool linear:{false,true}){
        ProcessOptions options;options.prefer_linear_memory=linear;
        Process p(FunctionExecutor{[](Cpu&,Memory&){throw std::runtime_error("unexpected source execution");return false;}},options);
        p.load(wa2::aot::initial_image());const auto baseline=p.memory.regions().size();
        const auto call=[&](const char* name,std::initializer_list<U32> values){return p.callback(p.resolve("kernel32.dll",name),std::span(values.begin(),values.size()));};
        auto heap=call("HeapCreate",{0,0,0});CHECK(heap);constexpr U32 Bytes=2u*1024*1024;
        auto small_a=call("HeapAlloc",{heap,0,32768}),small_b=call("HeapAlloc",{heap,0,32768});CHECK(small_a && small_b);
        CHECK(call("HeapFree",{heap,0,small_a}) && call("HeapFree",{heap,0,small_b}));
        auto large=call("HeapAlloc",{heap,0,Bytes});CHECK(large && call("HeapFree",{heap,0,large}));
        CHECK(call("HeapAlloc",{heap,0,Bytes})==large && call("HeapFree",{heap,0,large}));++checks;
        CHECK(call("HeapDestroy",{heap}) && !wr::large_heap_cache_statistics(p).idle_large_bytes && p.memory.regions().size()==baseline);++checks;
        heap=call("HeapCreate",{0,0,0});CHECK(heap);
        const auto initial=p.memory.native_operations();U32 previous{};
        for(unsigned n=0;n<16;++n){const auto at=call("HeapAlloc",{heap,0,Bytes});CHECK(at && (!previous || at==previous));previous=at;
            p.memory.store(at,0x12345678,32);p.memory.store(at+Bytes-4,0xaabbccdd,32);
            CHECK(call("HeapSize",{heap,0,at})==Bytes && call("HeapFree",{heap,0,at}));
            CHECK(call("HeapSize",{heap,0,at})==0xffffffffu && !call("HeapFree",{heap,0,at}));++reuse_cycles;
        }
        const auto final=p.memory.native_operations();if(linear)CHECK(final.commits-initial.commits==1 && final.decommits==initial.decommits);
        CHECK(wr::large_heap_cache_statistics(p).idle_large_bytes==Bytes && wr::large_heap_cache_statistics(p).idle_large_blocks==1);checks+=4;
        std::array<U32,3> pointers;for(auto& at:pointers){at=call("HeapAlloc",{heap,0,Bytes});CHECK(at);}
        for(auto at:pointers)CHECK(call("HeapFree",{heap,0,at}));
        CHECK(wr::large_heap_cache_statistics(p).idle_large_bytes==4u*1024*1024 && wr::large_heap_cache_statistics(p).idle_large_blocks==2 && p.memory.regions().size()==baseline+2);checks+=2;
        auto at=call("HeapAlloc",{heap,0,512u*1024});CHECK(at);p.memory.store(at,0x10203040,32);
        CHECK(call("HeapReAlloc",{heap,8,at,1024u*1024})==at && p.memory.load(at,32)==0x10203040);
        std::vector<std::uint8_t> grown(512u*1024);p.memory.copy_out(at+512u*1024,grown);CHECK(std::all_of(grown.begin(),grown.end(),[](auto b){return b==0;}));
        CHECK(call("HeapSize",{heap,0,at})==1024u*1024 && call("HeapFree",{heap,0,at}));checks+=3;
        const auto poison=at;p.memory.protect(poison,4096,Memory::Read);
        const auto evictions=wr::large_heap_cache_statistics(p).large_block_evictions;at=call("HeapAlloc",{heap,8,Bytes});CHECK(at && wr::large_heap_cache_statistics(p).large_block_evictions==evictions+1);
        std::vector<std::uint8_t> zeroed(Bytes);p.memory.copy_out(at,zeroed);CHECK(std::all_of(zeroed.begin(),zeroed.end(),[](auto b){return b==0;}));
        CHECK(call("HeapFree",{heap,0,at}));checks+=3;
        at=call("HeapAlloc",{heap,0,5u*1024*1024});CHECK(at);const auto held=wr::large_heap_cache_statistics(p).idle_large_bytes;CHECK(call("HeapFree",{heap,0,at}) && wr::large_heap_cache_statistics(p).idle_large_bytes==held);++checks;
        CHECK(call("HeapDestroy",{heap}) && !wr::large_heap_cache_statistics(p).idle_large_blocks && !wr::large_heap_cache_statistics(p).idle_large_bytes && p.memory.regions().size()==baseline);checks+=2;
        std::array<U32,4> heaps;for(auto& h:heaps){h=call("HeapCreate",{0,0,0});at=call("HeapAlloc",{h,0,Bytes});CHECK(at && call("HeapFree",{h,0,at}));}
        CHECK(wr::large_heap_cache_statistics(p).idle_large_blocks==2 && wr::large_heap_cache_statistics(p).idle_large_bytes==4u*1024*1024);
        for(auto h:heaps)CHECK(call("HeapDestroy",{h}));CHECK(!wr::large_heap_cache_statistics(p).idle_large_bytes && p.memory.regions().size()==baseline);checks+=2;
        heap=call("HeapCreate",{0,0,4096});CHECK(!call("HeapAlloc",{heap,0,32768}) && call("HeapDestroy",{heap}));++checks;
        heap=call("HeapCreate",{0,0,0});at=call("HeapAlloc",{heap,0,Bytes});CHECK(call("HeapFree",{heap,0,at}));
        wr::set_large_heap_cache_enabled(p,false);at=call("HeapAlloc",{heap,0,Bytes});CHECK(at && !wr::large_heap_cache_statistics(p).idle_large_blocks);
        CHECK(call("HeapFree",{heap,0,at}) && !wr::large_heap_cache_statistics(p).idle_large_bytes && call("HeapDestroy",{heap}) && p.memory.regions().size()==baseline);checks+=2;
        CHECK(!p.executed_instructions);
    }
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.large-heap-reuse.v1\",\"checks\":"<<checks<<",\"reuse_cycles\":"<<reuse_cycles
        <<",\"maximum_idle_blocks\":2,\"maximum_idle_bytes\":4194304,\"zero_fill_and_growth_preserved\":true,\"protected_idle_block_rejected\":true,\"destroy_releases_cache\":true,\"regions_restored\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS bounded large resource buffers: "<<checks<<" allocator checks and "<<reuse_cycles<<" reuse cycles.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
