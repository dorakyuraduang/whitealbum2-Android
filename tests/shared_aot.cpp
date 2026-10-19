#include "functions.hpp"
#include "native_math.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>

bool shared_aot_oracle(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool shared_aot_oracle_known(std::uint32_t);
void shared_aot_initialize_tables(wr::Memory&);
namespace a=wa2::aot;
constexpr std::uint32_t Code=0x401000,CodeSize=0xa0000,Stack=0x1000000,Object=0x1100000,Return=0xf0001234;
struct Region {std::uint32_t at,size;};
constexpr Region regions[]={{0x4a1000,0xff000},{0xa30000,0x20000},{Stack,0x10000},{Object,0x10000}};
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
#include "native_loop_snapshots.hpp"
#include "../generated/shared-aot/semantic_boundaries.hpp"
struct Failure {bool raised{};wr::FaultKind kind{};std::uint32_t address{};bool operator==(const Failure&)const=default;};
std::uint64_t timestamp_counter;
std::uint64_t snapshot_timestamp(){return timestamp_counter+=16667;}
struct Fixture {
    wr::Memory memory;
    a::Program program{memory};
    wr::Cpu state;
    bool services{};
    unsigned service_calls{};
    std::vector<std::uint8_t> zeros=std::vector<std::uint8_t>(0x100000);
    Fixture(){
        memory.map(Code,CodeSize,wr::Memory::Read|wr::Memory::Write);
        for(const auto& f:a::functions())for(const auto& span:f.code)memory.initialize(span.address,span.bytes);
        shared_aot_initialize_tables(memory);
        memory.protect(Code,CodeSize,wr::Memory::Read|wr::Memory::Execute);
        for(auto r:regions)memory.map(r.at,r.size,wr::Memory::Read|wr::Memory::Write);
    }
    void reset(std::uint32_t entry,unsigned variant){
        services=false;service_calls=0;
        for(auto r:regions)memory.copy_in(r.at,{zeros.data(),r.size});
        state=wr::Cpu{};state.eip=entry;
        std::mt19937 random(entry+variant);
        for(unsigned i=0;i<8;++i)state.r[i]=variant==0?0:variant==1?Object+0x100+i*0x20:random()%8;
        if(variant==2)state.r[wr::EAX]=state.r[wr::ESI]=state.r[wr::EDI]=Object+0x100;
        state.r[wr::ESP]=Stack+0x8000;
        state.flags=(random()&wr::STATUS_FLAGS)|0x202;
        state.defined_flags=random()&wr::STATUS_FLAGS;
        // Integer functions must preserve arbitrary x87/XMM payloads too.
        // They are opaque bits here; no host floating-point operation runs.
        for(unsigned at=32;at<state.fp.bytes.size();++at)state.fp.bytes[at]=std::uint8_t(random());
        state.fs_base=Object;state.fs_valid=true;
        memory.store(state.r[wr::ESP],Return,32);
        for(unsigned i=1;i<16;++i)memory.store(state.r[wr::ESP]+i*4,variant==1?Object+0x100:variant==2?i%8:0,32);
        // Synthetic inputs, not original game data. Nonzero globals exercise
        // arithmetic and table reads without opening the source executable.
        memory.store(0x4be000,60,32);
        memory.store(0x521930,0x12345678,32);
    }
    Failure run(bool native,std::uint64_t& budget,bool unmetered=false){
        a::NativeMathReferenceScope original_function_bodies;
        timestamp_counter=0;wr::timestamp_source=snapshot_timestamp;
        try{
            if(native){
                while(state.eip!=Return){
                    if(services && state.eip==0xf0000010){++service_calls;state.r[wr::EAX]=37;state.eip=wr::pop(state,memory);continue;}
                    const bool known=unmetered?program.invoke_unmetered(state):program.invoke(state,budget);
                    if(!known)throw wr::GuestFault(wr::FaultKind::untranslated,state.eip,"unregistered function return target");
                }
            }else{
                while(state.eip!=Return){
                    if(services && state.eip==0xf0000010){++service_calls;state.r[wr::EAX]=37;state.eip=wr::pop(state,memory);continue;}
                    if(!shared_aot_oracle_known(state.eip))
                        throw wr::GuestFault(wr::FaultKind::untranslated,state.eip,"untranslated oracle target");
                    auto scope=memory.execution_scope();
                    wr::tick(budget,state.eip);
                    if(!shared_aot_oracle(state,memory,budget))
                        throw wr::GuestFault(wr::FaultKind::untranslated,state.eip,"untranslated oracle target");
                }
            }
        }catch(const wr::GuestFault& fault){return {true,fault.kind,fault.address};}
        return {};
    }
};
void compare(Fixture& left,Fixture& right){
    const auto& s=left.state;const auto& t=right.state;
    CHECK(s.r==t.r && s.eip==t.eip && s.flags==t.flags && s.defined_flags==t.defined_flags);
    if(s.fp.bytes!=t.fp.bytes){
        unsigned shown{};for(unsigned i=0;i<s.fp.bytes.size() && shown<24;++i)if(s.fp.bytes[i]!=t.fp.bytes[i]){
            std::cerr<<"FP byte "<<i<<" actual "<<unsigned(s.fp.bytes[i])<<" expected "<<unsigned(t.fp.bytes[i])<<'\n';++shown;
        }
    }
    CHECK(s.fp.bytes==t.fp.bytes && s.selectors==t.selectors);
    CHECK(s.fs_base==t.fs_base && s.gs_base==t.gs_base && s.fs_valid==t.fs_valid && s.gs_valid==t.gs_valid);
    std::vector<std::uint8_t> l(0x100000),r(0x100000);
    for(auto region:regions){
        left.memory.copy_out(region.at,{l.data(),region.size});right.memory.copy_out(region.at,{r.data(),region.size});
        CHECK(std::equal(l.begin(),l.begin()+region.size,r.begin()));
    }
}
int main(int argc,char** argv){try{
    if(argc==3 && std::string(argv[2])=="--grid-only"){
        const auto cases=native_loop_test::run();std::ofstream report(argv[1]);
        report<<"{\"full_native_loop_snapshots\":"<<cases<<",\"native_grid_arithmetic_hardware_cases\":"<<native_loop_test::native_arithmetic_accepted
            <<",\"native_grid_arithmetic_unchanged_declines\":"<<native_loop_test::native_arithmetic_declined
            <<",\"native_grid_stored_hardware_cases\":"<<native_loop_test::native_stored_arithmetic_accepted
            <<",\"native_grid_stored_unchanged_declines\":"<<native_loop_test::native_stored_arithmetic_declined
            <<",\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
        std::cout<<"PASS bounded native grid state and arithmetic snapshots.\n";return 0;
    }
    CHECK(argc==2);Fixture native,oracle,unmetered;
    unsigned entry_checks{};
    for(std::uint32_t pc=Code;pc<Code+CodeSize;++pc){
        CHECK(bool(a::find(pc))==shared_aot_oracle_known(pc));
        if(a::find(pc))++entry_checks;
    }
    unsigned cases{},completed{},faults{},budgets{},unmetered_cases{},direct_call_completions{};
    for(const auto& function:a::functions())for(unsigned variant=0;variant<3;++variant){
        native.reset(function.entry,variant);oracle.reset(function.entry,variant);
        std::uint64_t b=512,c=512;
        const auto actual=native.run(true,b),expected=oracle.run(false,c);
        if(!(actual==expected && b==c)){
            std::cerr<<"difference at 0x"<<std::hex<<function.entry<<std::dec<<" variant "<<variant
                     <<" actual "<<int(actual.kind)<<' '<<actual.address<<" expected "<<int(expected.kind)<<' '<<expected.address
                     <<" budgets "<<b<<' '<<c<<'\n';
            CHECK(false);
        }
        try{compare(native,oracle);}catch(...){std::cerr<<"state difference at 0x"<<std::hex<<function.entry<<std::dec<<" variant "<<variant<<'\n';throw;}
        ++cases;
        if(!actual.raised){++completed;if(!function.callees.empty())++direct_call_completions;}
        else if(actual.kind==wr::FaultKind::budget)++budgets;else ++faults;
        // Never run a synthetic unbounded loop without a meter. The identical
        // starting snapshot must first finish or fault under the checked run.
        if(!actual.raised || actual.kind!=wr::FaultKind::budget){
            unmetered.reset(function.entry,variant);std::uint64_t unused{};
            CHECK(unmetered.run(true,unused,true)==expected);compare(unmetered,oracle);++unmetered_cases;
        }
    }
    // Exact instruction budgets also cover short exits and nested direct calls.
    for(auto entry:{0x401230u,0x401260u,0x4018e3u})if(a::find(entry))for(unsigned limit=0;limit<48;++limit){
        native.reset(entry,2);oracle.reset(entry,2);std::uint64_t b=limit,c=limit;
        CHECK(native.run(true,b)==oracle.run(false,c));CHECK(b==c);compare(native,oracle);++cases;
    }
    // Enter every declared semantic boundary directly: it must fail before
    // changing a register, memory or FP state, under both execution policies.
    unsigned contract_boundaries{};
    for(auto pc:a::pending_transcendental_sites){
        native.reset(pc,2);oracle.reset(pc,2);unmetered.reset(pc,2);
        std::uint64_t b=8,c=8,unused{};
        const auto actual=native.run(true,b),expected=oracle.run(false,c);
        CHECK(actual.raised && actual.kind==wr::FaultKind::unsupported && actual.address==pc);
        CHECK(actual==expected && b==7 && c==7);compare(native,oracle);
        CHECK(unmetered.run(true,unused,true)==expected);compare(unmetered,oracle);
        ++contract_boundaries;
    }
    unsigned protected_cases{},table_cases{};
    for(unsigned kind=0;kind<3;++kind){
        const auto entry=kind==2?0x401050u:0x401230u;
        for(auto* fixture:{&native,&oracle}){
            fixture->reset(entry,0);
            if(kind==0)fixture->memory.protect(0x4be000,4096,wr::Memory::Read);
            else{
                // Deliberately unaligned stack words crossing a 4 KiB boundary.
                fixture->state.r[wr::ESP]=Stack+(kind==1?0xffb:0x1001);
                fixture->memory.store(fixture->state.r[wr::ESP],Return,32);
                fixture->memory.store(fixture->state.r[wr::ESP]+4,7,32);
                fixture->memory.store(fixture->state.r[wr::ESP]+8,9,32);
                if(kind==2)fixture->memory.protect(Stack+4096,4096,wr::Memory::Read);
            }
        }
        std::uint64_t b=128,c=128;CHECK(native.run(true,b)==oracle.run(false,c));CHECK(b==c);compare(native,oracle);++protected_cases;
        for(auto* fixture:{&native,&oracle}){
            fixture->memory.protect(0x4be000,4096,wr::Memory::Read|wr::Memory::Write);
            fixture->memory.protect(Stack+4096,4096,wr::Memory::Read|wr::Memory::Write);
        }
    }
    // Exercise all four cases of an original local jump table. The command
    // bytes are synthetic; the branch destinations come from the cached CFG.
    for(unsigned command=0;command<4;++command){
        for(auto* fixture:{&native,&oracle}){
            fixture->reset(0x448300,1);
            fixture->state.r[wr::ECX]=Object+0x100;fixture->state.r[wr::EBX]=Object+0x104;
            fixture->state.r[wr::ESI]=Object+0x108;fixture->state.r[wr::EDI]=Object+0x200;
            fixture->memory.store(Stack+0x8004,Object+0x1000,32);
            fixture->memory.store(Object+0x200,command,16);
            fixture->memory.store(Object+0x208,1,16);
            if(command==3){fixture->memory.store(Object+0x108,1,32);fixture->memory.store(Object+0x1000,1,8);}
        }
        std::uint64_t b=512,c=512;CHECK(native.run(true,b)==oracle.run(false,c));CHECK(b==c);compare(native,oracle);++table_cases;
    }
    for(auto* fixture:{&native,&oracle}){
        fixture->reset(0x448300,1);
        constexpr std::uint8_t unknown_target[]={0xef,0xbe,0xad,0xde};
        fixture->memory.initialize(0x4483d0,unknown_target);
    }
    std::uint64_t bad_native=512,bad_oracle=512;
    const auto bad=native.run(true,bad_native);
    CHECK(bad==oracle.run(false,bad_oracle));CHECK(bad.raised && bad.kind==wr::FaultKind::untranslated && bad.address==0xdeadbeef);
    CHECK(bad_native==bad_oracle);compare(native,oracle);
    shared_aot_initialize_tables(native.memory);shared_aot_initialize_tables(oracle.memory);
    // Imports resume AFTER the call, preserving the caller's original stack.
    // Dynamic jumps can reach a known compiled entry, a host service, or fail
    // explicitly. The same snapshots are used by the instruction oracle.
    unsigned boundary_cases{};
    for(unsigned kind=0;kind<4;++kind){
        for(auto* fixture:{&native,&oracle,&unmetered}){
            fixture->reset(kind==0?0x401300:0x450819,0);
            fixture->services=true;
            fixture->memory.store(0x4a137c,0xf0000010,32);
            fixture->memory.store(Stack+0x8004,5,32);
            fixture->state.r[wr::EAX]=kind==1?0x401260:kind==2?0xf0000010:0x401301;
        }
        std::uint64_t b=128,c=128,unused{};
        const auto expected=oracle.run(false,c);
        CHECK(native.run(true,b)==expected);CHECK(b==c);compare(native,oracle);
        CHECK(unmetered.run(true,unused,true)==expected);compare(unmetered,oracle);
        if(kind==0){CHECK(native.service_calls==1);CHECK(native.memory.load(0xa391ac,32)==42);}
        if(kind==1){CHECK(!expected.raised && native.state.r[wr::EAX]==5 && !native.service_calls);}
        if(kind==2){CHECK(!expected.raised && native.state.r[wr::EAX]==37 && native.service_calls==1);}
        if(kind==3){CHECK(expected.raised && expected.address==0x401301 && !native.service_calls);}
        ++boundary_cases;
    }
    // Independent expected behavior supplements comparison with the old IR.
    for(auto parameter:{-2,-1,0,1,3,120}){
        native.reset(0x401260,0);native.memory.store(native.state.r[wr::ESP]+4,std::uint32_t(parameter),32);
        std::uint64_t budget=128;CHECK(!native.run(true,budget).raised);
        CHECK(std::int32_t(native.state.r[wr::EAX])==(parameter==-2?30:parameter==-1?15:parameter));
    }
    native.reset(0x401230,0);native.memory.store(native.state.r[wr::ESP]+4,7,32);native.memory.store(native.state.r[wr::ESP]+8,9,32);
    std::uint64_t budget=128;CHECK(!native.run(true,budget).raised);
    CHECK(native.state.r[wr::EAX]==1 && native.memory.load(0x4be03c,32)==7 && native.memory.load(0x4be044,32)==9);
    native.reset(0xdeadbeef,0);const auto unknown=native.state;budget=20;
    CHECK(!native.program.invoke(native.state,budget));CHECK(native.state.r==unknown.r && native.state.eip==unknown.eip && budget==20);
    // Invalidate an already cached proof; modified/writable code must fail.
    native.reset(0x401230,0);const auto* f=a::find(0x401230);CHECK(f);
    auto changed=f->code.front().bytes.front()^0xff;const auto byte=std::uint8_t(changed);
    native.memory.initialize(0x401230,{&byte,1});
    auto failure=native.run(true,budget);CHECK(failure.raised && failure.kind==wr::FaultKind::unsupported);
    native.memory.initialize(f->code.front().address,f->code.front().bytes);
    native.memory.protect(Code,CodeSize,wr::Memory::Read|wr::Memory::Write|wr::Memory::Execute);
    failure=native.run(true,budget);CHECK(failure.raised && failure.kind==wr::FaultKind::unsupported);
    CHECK(completed>100 && direct_call_completions>10 && unmetered_cases>100);
    const auto native_loop_cases=native_loop_test::run();
    std::ofstream report(argv[1]);
    report<<"{\"schema\":\"wa2.shared-aot-differential.v1\",\"functions\":"<<a::functions().size()<<",\"metered_cases\":"<<cases
          <<",\"completed_synthetic_calls\":"<<completed<<",\"direct_call_completions\":"<<direct_call_completions
          <<",\"matching_faults\":"<<faults<<",\"matching_budget_exits\":"<<budgets<<",\"unmetered_cases\":"<<unmetered_cases
          <<",\"compiled_entry_checks\":"<<entry_checks<<",\"host_and_indirect_boundary_cases\":"<<boundary_cases
          <<",\"full_native_loop_snapshots\":"<<native_loop_cases
          <<",\"native_grid_arithmetic_hardware_cases\":"<<native_loop_test::native_arithmetic_accepted
          <<",\"native_grid_arithmetic_unchanged_declines\":"<<native_loop_test::native_arithmetic_declined
          <<",\"native_grid_stored_hardware_cases\":"<<native_loop_test::native_stored_arithmetic_accepted
          <<",\"native_grid_stored_unchanged_declines\":"<<native_loop_test::native_stored_arithmetic_declined
          <<",\"unestablished_instruction_boundaries_checked\":"<<contract_boundaries
          <<",\"raw_state_comparisons_use_original_math_function_bodies\":true,\"native_math_has_separate_abi_checks\":true"
          <<",\"independent_expected_behavior_cases\":7,\"unknown_target_rejected\":true,\"modified_code_rejected\":true,\"writable_code_rejected\":true,"
            "\"unknown_jump_table_target_rejected\":true,\"protected_and_cross_page_cases\":"<<protected_cases<<",\"jump_table_cases\":"<<table_cases<<','
          <<"\"source_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"complete_gameplay_verified\":false,\"passed\":true}\n";
    std::cout<<"PASS shared native functions: "<<cases<<" checked / "<<unmetered_cases<<" unmetered differential cases, "<<completed<<" completed synthetic calls.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
