// Exact, bounded CRT leaf snapshots. No startup, windows, resources or devices.
#include "process.hpp"
#include "work_samples.hpp"
#include <winrecomp/win32_state.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace a=wa2::aot;
constexpr wr::U32 Return=0xf0001234;
struct Result {wr::Cpu cpu;std::array<std::uint8_t,256> stack;wr::U32 error;unsigned entries{},apis{};};
bool dispatch(wr::Cpu& cpu,wr::Memory& memory,void* owner){
    auto& p=*static_cast<wr::Process*>(owner);CHECK(&cpu==&p.cpu && &memory==&p.memory);return p.dispatch_leaf_api();
}
int main(int argc,char** argv){try{
    CHECK(argc==2);a::Runtime runtime;auto& p=runtime.process();
    // Nested callbacks may overwrite the source stack, but must not overwrite
    // the enclosing API's copied arguments or its diagnostic argument record.
    p.register_api("fixture","nested",1,[](wr::Process& q){CHECK(q.argument_snapshot().size()==1 && q.argument_snapshot()[0]==91);return 7u;});
    p.register_api("fixture","wide",64,[](wr::Process& q){
        const auto values=q.argument_snapshot();CHECK(values.size()==64);wr::U32 total{};
        for(unsigned n=0;n<64;++n){CHECK(values[n]==n*0x1020304u+1);total+=values[n];}
        q.memory.store(q.cpu.r[wr::ESP]+4,0xfeedcafe,32);
        CHECK(q.callback(q.resolve("fixture","nested"),std::array<wr::U32,1>{91})==7);
        CHECK(q.argument_snapshot().data()==values.data() && q.argument_snapshot()[0]==1);return total;
    });
    runtime.load();a::Program program(p.memory);
    const auto slot=p.callback(p.resolve("kernel32.dll","FlsAlloc"),std::array<wr::U32,1>{0});CHECK(slot!=0xffffffffu);
    const auto data=p.allocate_bytes(4096);p.state().tls[0]=p.resolve("kernel32.dll","FlsGetValue");p.state().tls_used.insert(0);
    p.callback(p.resolve("kernel32.dll","FlsSetValue"),std::array<wr::U32,2>{slot,data});
    p.memory.store(0x4d1cc4,0,32);p.memory.store(0x4d1cc0,slot,32);
    p.memory.store(data+0x1f8,p.resolve("kernel32.dll","EncodePointer"),32);
    p.memory.store(data+0x1fc,p.resolve("kernel32.dll","DecodePointer"),32);
    auto base=p.cpu;base.r[wr::ESP]-=4096; // Leave room on both sides for raw stack comparison.
    const auto run=[&](wr::U32 entry,wr::U32 input,bool direct,bool metered=false){
        p.cpu=base;p.cpu.eip=entry;p.set_error(123);
        p.memory.store(base.r[wr::ESP],Return,32);p.memory.store(base.r[wr::ESP]+4,input,32);
        std::uint64_t budget=4096;Result result{};const auto apis=p.api_calls;
        while(p.cpu.eip!=Return){
            CHECK(result.entries<64);bool known;
            if(metered)known=program.invoke(p.cpu,budget);
            else if(direct){a::LeafServiceScope scope{dispatch,&p};known=program.invoke_unmetered(p.cpu);}
            else known=program.invoke_unmetered(p.cpu);
            if(known)++result.entries;
            else CHECK(p.dispatch_api());
        }
        result.cpu=p.cpu;result.error=p.last_error;result.apis=unsigned(p.api_calls-apis);
        p.memory.copy_out(base.r[wr::ESP]-128,result.stack);return result;
    };
    const auto same=[](const Result& a,const Result& b){
        CHECK(a.cpu.r==b.cpu.r && a.cpu.eip==b.cpu.eip && a.cpu.flags==b.cpu.flags && a.cpu.defined_flags==b.cpu.defined_flags);
        CHECK(a.cpu.fp.bytes==b.cpu.fp.bytes && a.cpu.selectors==b.cpu.selectors && a.cpu.fs_base==b.cpu.fs_base && a.cpu.gs_base==b.cpu.gs_base && a.cpu.fs_valid==b.cpu.fs_valid && a.cpu.gs_valid==b.cpu.gs_valid);
        CHECK(a.stack==b.stack && a.error==b.error && a.apis==b.apis);
    };
    unsigned cases{},ordinary_entries{},direct_entries{},source_calls{};
    for(auto entry:{0x47b701u,0x47b77cu})for(auto input:{0u,1u,0x12345678u,0xffffffffu}){
        const auto checked=run(entry,input,false,true),ordinary=run(entry,input,false),direct=run(entry,input,true);
        same(checked,ordinary);same(ordinary,direct);CHECK(direct.entries<ordinary.entries);
        ordinary_entries+=ordinary.entries;direct_entries+=direct.entries;source_calls+=direct.apis;++cases;
    }
    // A cached source function taking one stdcall argument, reached by a
    // source function pointer. Negative values return zero without resources.
    CHECK(a::find(0x45e6e0));p.memory.store(data+0x1f8,0x45e6e0,32);
    unsigned indirect_before{},indirect_after{};
    for(auto input:{0x80000000u,0xfffffffdu,0xffffffffu}){
        Result outer;{a::NativeIndirectReferenceScope reference;outer=run(0x47b701,input,true);}
        const auto direct=run(0x47b701,input,true);same(outer,direct);
        CHECK(direct.cpu.r[wr::EAX]==0 && direct.entries<outer.entries);indirect_before+=outer.entries;indirect_after+=direct.entries;
    }
    p.memory.store(data+0x1f8,p.resolve("kernel32.dll","EncodePointer"),32);
    // FLS destructors can enter source callbacks after ExitThread requested a
    // yield. Such callbacks retain that pending request and the ordinary API
    // boundaries; a leaf must not mistake the old request for its own change.
    p.state().yield_requested=true;
    const auto cleanup_reference=run(0x47b701,0x12345678,false),cleanup=run(0x47b701,0x12345678,true);
    same(cleanup_reference,cleanup);CHECK(cleanup.entries==cleanup_reference.entries && p.state().yield_requested);
    p.state().yield_requested=false;
    // The fixed allowance must stop a pure-service loop at a regular host
    // boundary. Unknown targets never consume CPU state or become callbacks.
    p.cpu=base;p.cpu.eip=p.resolve("kernel32.dll","GetCurrentThreadId");p.memory.store(base.r[wr::ESP],Return,32);
    unsigned attempts{};
    {a::LeafServiceScope scope{[](wr::Cpu&,wr::Memory&,void* owner){++*static_cast<unsigned*>(owner);return true;},&attempts};
        for(unsigned i=0;i<40;++i)CHECK(a::call_leaf_service(p.cpu,p.memory)==(i<32));}
    CHECK(attempts==32 && !a::current_leaf_services);
    unsigned native_attempts{},leaf_attempts{};
    {auto memory_scope=p.memory.execution_scope();
        a::NativeCallScope native{[](wr::Cpu&,wr::Memory&,wr::Memory::ExecutionScope&,void* owner){++*static_cast<unsigned*>(owner);return true;},&native_attempts};
        a::LeafServiceScope leaves{[](wr::Cpu&,wr::Memory&,void* owner){++*static_cast<unsigned*>(owner);return false;},&leaf_attempts};
        for(unsigned n=0;n<20;++n)CHECK(a::call_source_indirect(p.cpu,p.memory,memory_scope)==(n<16));
    }
    CHECK(native_attempts==16 && leaf_attempts==16 && !a::current_native_calls);
    unsigned stack_cases{};const auto frame=p.allocate_bytes(12288);const auto saved=p.cpu;
    const auto call_wide=[&](wr::U32 stack){
        p.cpu=base;p.cpu.r[wr::ESP]=stack;p.cpu.eip=p.resolve("fixture","wide");p.memory.store(stack,Return,32);wr::U32 total{};
        for(unsigned n=0;n<64;++n){const auto value=n*0x1020304u+1;p.memory.store(stack+4+n*4,value,32);total+=value;}
        CHECK(p.dispatch_api() && p.cpu.r[wr::EAX]==total && p.cpu.r[wr::ESP]==stack+260 && p.cpu.eip==Return);
        const auto& transfer=p.recent_transfers[p.recent_transfers.size()-2];CHECK(transfer.api=="fixture!wide" && transfer.argument_count==64 && transfer.arguments[0]==1);++stack_cases;
    };
    call_wide(frame+512);const auto slow_before=p.api_stack_slow_snapshots;call_wide(frame+4096-127);CHECK(p.api_stack_slow_snapshots==slow_before+1);
    p.memory.protect(frame+4096,4096,0);
    for(const auto stack:{frame+4096-2,frame+4096-100,0xfffffffeu}){
        p.cpu=base;p.cpu.r[wr::ESP]=stack;p.cpu.eip=p.resolve("fixture","wide");const auto before=p.cpu;
        wr::FaultKind expected_kind{};wr::U32 expected_address{};bool expected{};
        try{p.memory.load(stack,32);for(unsigned n=0;n<64;++n)(void)p.argument(n);p.memory.check(stack,260,wr::Memory::Read);}
        catch(const wr::GuestFault& fault){expected=true;expected_kind=fault.kind;expected_address=fault.address;}
        CHECK(expected);bool caught{};try{p.dispatch_api();}catch(const wr::GuestFault& fault){caught=fault.kind==expected_kind && fault.address==expected_address;}
        CHECK(caught && p.cpu.r==before.r && p.cpu.eip==before.eip);++stack_cases;
    }
    p.memory.protect(frame+4096,4096,wr::Memory::Read|wr::Memory::Write);p.cpu=saved;
    const std::array entries{a::Function{0x401000},a::Function{0x402000}};a::FunctionWorkSamples samples(entries);
    for(unsigned i=0;i<255;++i)if(auto* counter=samples.begin(0))samples.finish(*counter,100);
    CHECK(samples.counters[0].calls==255 && samples.counters[0].samples==3 && samples.counters[0].ns==300 && samples.estimate(samples.counters[0])==25500);
    for(unsigned n=0;n<300;++n){const auto pc=0x43b7a0u+n*3;if(auto* counter=samples.begin_location(0x43b7a0,pc,0x7f))samples.finish(*counter,100);}
    CHECK(std::count_if(samples.locations.begin(),samples.locations.end(),[](const auto& value){return value.calls!=0;})==256);
    CHECK(!samples.begin_location(0x401000,0x43b7a0,0x7f));
    {a::FunctionWorkScope scope{&samples};CHECK(a::current_work_samples==&samples);}CHECK(!a::current_work_samples);
    p.cpu.eip=p.resolve("kernel32.dll","Sleep");const auto before=p.cpu;CHECK(!p.dispatch_leaf_api());CHECK(p.cpu.r==before.r && p.cpu.eip==before.eip);
    p.cpu.eip=0xdeadbeef;CHECK(!p.dispatch_leaf_api() && p.cpu.eip==0xdeadbeef);
    std::uint64_t durations[2]{};constexpr unsigned repetitions=256,rounds=5;
    // A few thousand small leaf calls, capped by the harness timeout. These
    // numbers measure only ABI dispatch; they are not a frame-rate estimate.
    for(unsigned round=0;round<rounds;++round)for(unsigned order=0;order<2;++order){
        const unsigned direct=(round+order)&1;const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<repetitions;++i){const auto result=run(i&1?0x47b701u:0x47b77cu,i*0x9e3779b9u,direct);CHECK(result.apis==4);}
        durations[direct]+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    }
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.source-leaf-dispatch.v1\",\"exact_cpu_stack_thread_state_cases\":"<<cases
        <<",\"ordinary_function_entries\":"<<ordinary_entries<<",\"direct_function_entries\":"<<direct_entries<<",\"same_source_api_calls\":"<<source_calls
        <<",\"maximum_leaf_calls_before_outer_boundary\":32,\"waits_and_unknown_targets_decline\":true,\"bounded_replay_calls_per_mode\":"<<repetitions*rounds
        <<",\"indirect_exact_cpu_stack_cases\":3,\"indirect_outer_entries_before\":"<<indirect_before<<",\"indirect_outer_entries_after\":"<<indirect_after
        <<",\"indirect_and_leaf_share_allowance\":true,\"argument_snapshot_fault_and_callback_cases\":"<<stack_cases<<",\"diagnostic_transfer_capacity\":"<<p.recent_transfers.size()
        <<",\"ordinary_replay_ns\":"<<durations[0]<<",\"direct_replay_ns\":"<<durations[1]
        <<",\"whole_game_fps_measured\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS exact shared CRT leaf calls: "<<ordinary_entries<<" -> "<<direct_entries<<" function entries; same "<<source_calls<<" APIs.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
