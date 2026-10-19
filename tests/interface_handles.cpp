#include "process.hpp"
#include "../platform/compat/interface_handles.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
using namespace wr;
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned checks{};std::array<std::array<std::uint64_t,2>,8> times{},commits{};
    for(bool linear:{false,true}){
        ProcessOptions options;options.prefer_linear_memory=linear;
        Process p(FunctionExecutor{[](Cpu&,Memory&){throw std::runtime_error("source code must not execute");return false;}},options);
        p.load(wa2::aot::initial_image());const auto baseline=p.memory.regions().size();
        wa2::platform::compat::InterfaceHandles handles(p);
        std::set<U32> live;std::vector<U32> ordered;
        for(unsigned n=0;n<2112;++n){const auto table=n&1?0x412340u:0x423450u,at=handles.acquire(table);CHECK(live.insert(at).second);
            CHECK(p.memory.load(at,32)==table && p.memory.permissions(at)==Memory::Read);ordered.push_back(at);}
        CHECK(handles.bytes()==16384 && handles.live()==2112);checks+=3;
        for(auto at:ordered)handles.release(at);CHECK(handles.live()==0);
        bool rejected{};try{handles.release(ordered[0]);}catch(const std::logic_error&){rejected=true;}CHECK(rejected);++checks;
        const auto before=p.memory.native_operations();
        for(unsigned n=0;n<128;++n){const auto at=handles.acquire(0x412340);CHECK(p.memory.load(at,32)==0x412340);handles.release(at);}
        const auto after=p.memory.native_operations();CHECK(after.commits==before.commits && after.decommits==before.decommits && handles.reuses()>=128);++checks;
        auto at=handles.acquire(0x412340);handles.release(at);p.memory.protect(at&~4095u,4096,Memory::Read|Memory::Write);p.memory.store(at,0xbad,32);p.memory.protect(at&~4095u,4096,Memory::Read);
        rejected=false;try{handles.acquire(0x412340);}catch(const GuestFault& e){rejected=e.kind==FaultKind::unsupported;}CHECK(rejected);++checks;
        handles.clear();CHECK(p.memory.regions().size()==baseline);++checks;
        if(linear)for(unsigned pair=0;pair<8;++pair)for(unsigned order=0;order<2;++order){
            const auto pooled=(pair+order)&1;if(pooled){at=handles.acquire(0x412340);handles.release(at);}
            const auto operations=p.memory.native_operations();const auto start=std::chrono::steady_clock::now();
            for(unsigned n=0;n<32;++n){if(pooled){at=handles.acquire(0x412340);CHECK(p.memory.load(at,32)==0x412340);handles.release(at);}
                else{at=p.allocate_bytes(4);p.memory.store(at,0x412340,32);p.memory.protect(at,4096,Memory::Read);CHECK(p.memory.load(at,32)==0x412340);p.memory.release(at);}}
            times[pair][pooled]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            commits[pair][pooled]=p.memory.native_operations().commits-operations.commits;
        }
        handles.clear();CHECK(p.memory.regions().size()==baseline);
    }
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.interface-handle-memory.v1\",\"checks\":"<<checks<<",\"live_handles_checked\":4224,\"handles_per_sample\":32,\"paired_ns\":[";
    for(unsigned n=0;n<8;++n){if(n)out<<',';out<<'['<<times[n][0]<<','<<times[n][1]<<']';}out<<"],\"paired_native_commit_calls\":[";
    for(unsigned n=0;n<8;++n){if(n)out<<',';out<<'['<<commits[n][0]<<','<<commits[n][1]<<']';}
    out<<"],\"read_only_vtables_preserved\":true,\"modified_slot_rejected\":true,\"regions_restored\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS immutable interface slots, live identities, bounded reuse, mutation rejection and shutdown release.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
