// Bounded allocator semantics and temporary vertex-buffer reuse, in memory.
#include "process.hpp"
#include <winrecomp/win32_state.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
using namespace wr;
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned checks{},reuse_cycles{};std::array<std::array<std::uint64_t,2>,8> times{},commits{};
    for(bool linear:{false,true}){
        ProcessOptions options;options.prefer_linear_memory=linear;
        Process p(FunctionExecutor{[](Cpu&,Memory&){throw std::runtime_error("unexpected source code execution");return false;}},options);
        p.load(wa2::aot::initial_image());const auto baseline=p.memory.regions().size();
        const auto call=[&](const char* name,std::initializer_list<U32> values){return p.callback(p.resolve("kernel32.dll",name),std::span(values.begin(),values.size()));};
        auto heap=call("HeapCreate",{0,0,4096});CHECK(heap);
        const auto first=p.memory.native_operations();U32 previous{};
        for(unsigned n=0;n<128;++n){const auto pointer=call("HeapAlloc",{heap,8,80});CHECK(pointer && (!previous || previous==pointer));previous=pointer;
            for(unsigned at=0;at<80;++at)CHECK(p.memory.load(pointer+at,8)==0);
            p.memory.store(pointer,n+1,32);p.memory.store(pointer+76,0xaabbccdd,32);
            CHECK(call("HeapSize",{heap,0,pointer})==80 && call("HeapFree",{heap,0,pointer})==1);
            CHECK(call("HeapSize",{heap,0,pointer})==0xffffffffu && call("HeapFree",{heap,0,pointer})==0);++reuse_cycles;
        }
        const auto last=p.memory.native_operations();if(linear)CHECK(last.commits-first.commits==1 && last.decommits==first.decommits);
        CHECK(p.state().idle_heap_pools==1 && p.state().heaps.at(heap).blocks.empty() && p.state().heaps.at(heap).requested_bytes==0);
        CHECK(p.memory.regions().size()==baseline+1);checks+=5;
        auto a=call("HeapAlloc",{heap,8,32}),b=call("HeapAlloc",{heap,8,32});CHECK(a && b);p.memory.store(a,0x12345678,32);
        CHECK(!call("HeapReAlloc",{heap,0x18,a,4097}) && p.memory.load(a,32)==0x12345678);
        CHECK(call("HeapFree",{heap,0,b}));CHECK(call("HeapReAlloc",{heap,0x18,a,1024})==a);
        CHECK(call("HeapSize",{heap,0,a})==1024);for(unsigned at=32;at<1024;++at)CHECK(p.memory.load(a+at,8)==0);
        CHECK(call("HeapFree",{heap,0,a}));CHECK(call("HeapDestroy",{heap}) && p.memory.regions().size()==baseline && !p.state().idle_heap_pools);checks+=5;
        std::array<U32,20> heaps{};
        for(auto& h:heaps){h=call("HeapCreate",{0,0,0});a=call("HeapAlloc",{h,0,80});CHECK(a && call("HeapFree",{h,0,a}));}
        CHECK(p.state().idle_heap_pools==16 && p.state().peak_idle_heap_pools==16 && p.memory.regions().size()==baseline+16);
        for(auto h:heaps)CHECK(call("HeapDestroy",{h}));CHECK(p.memory.regions().size()==baseline && !p.state().idle_heap_pools);checks+=2;
        heap=call("HeapCreate",{0,0,0});a=call("HeapAlloc",{heap,8,80});CHECK(call("HeapFree",{heap,0,a}));
        p.memory.protect(a&~4095u,4096,Memory::Read);const auto evictions=p.state().heap_pool_evictions;
        b=call("HeapAlloc",{heap,8,80});CHECK(b && p.memory.permissions(b)==(Memory::Read|Memory::Write) && p.memory.load(b,32)==0);
        CHECK(p.state().heap_pool_evictions==evictions+1 && call("HeapFree",{heap,0,b}) && call("HeapDestroy",{heap}));
        CHECK(p.memory.regions().size()==baseline && !p.state().idle_heap_pools);checks+=3;
        heap=call("HeapCreate",{0,0,0});
        const auto cycle=[&]{const auto pointer=call("HeapAlloc",{heap,8,80});CHECK(pointer);p.memory.store(pointer,0x12345678,32);
            CHECK(p.memory.load(pointer,32)==0x12345678 && call("HeapFree",{heap,0,pointer}));};
        if(linear)for(unsigned pair=0;pair<times.size();++pair)for(unsigned order=0;order<2;++order){const auto cached=(pair+order)&1;
            p.state().retain_idle_heap_pools=cached;cycle();const auto before=p.memory.native_operations();
            const auto begin=std::chrono::steady_clock::now();for(unsigned n=0;n<16;++n)cycle();
            times[pair][cached]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
            commits[pair][cached]=p.memory.native_operations().commits-before.commits;
            CHECK(commits[pair][cached]==(cached?0u:16u));
        }
        p.state().retain_idle_heap_pools=true;CHECK(call("HeapDestroy",{heap}));CHECK(p.memory.regions().size()==baseline && !p.state().idle_heap_pools);
        CHECK(p.executed_instructions==0);checks+=2;
    }
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.bounded-heap-reuse.v1\",\"checks\":"<<checks<<",\"reuse_cycles\":"<<reuse_cycles
        <<",\"maximum_idle_pools\":16,\"maximum_idle_bytes\":1048576,\"one_idle_pool_per_heap\":true,\"calls_per_sample\":16,\"paired_ns\":[";
    for(unsigned n=0;n<times.size();++n){if(n)out<<',';out<<'['<<times[n][0]<<','<<times[n][1]<<']';}
    out<<"],\"paired_native_commit_calls\":[";for(unsigned n=0;n<commits.size();++n){if(n)out<<',';out<<'['<<commits[n][0]<<','<<commits[n][1]<<']';}
    out<<"],\"zero_fill_and_reallocation_preserved\":true,\"protected_idle_pool_rejected\":true,\"destroy_releases_all_pools\":true,\"regions_restored\":true,\"graphics_device_created\":false,\"game_launched\":false,\"whole_game_fps_measured\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS bounded heap reuse, limits, zeroing, growth, permissions and destruction.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
