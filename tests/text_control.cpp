// Finite own-memory fixtures. No original executable, game, device or window.
#include "../recompiled/text_stack.hpp"
#include "../recompiled/text_jump_budget.hpp"
#include "../recompiled/process.hpp"
#include "../generated/shared-aot/bodies.hpp"
#include <fstream>
#include <iostream>
#include <optional>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("text control check at ")+std::to_string(__LINE__)+": "+#x);}while(false)
namespace a=wa2::aot;
using wr::U32;

struct Result {
    wr::Cpu cpu;U32 value{};std::optional<std::pair<wr::FaultKind,U32>> fault;
    std::string message;std::vector<std::uint8_t> bytes;std::uint64_t epoch{};
};
static bool same_cpu(const wr::Cpu& x,const wr::Cpu& y){
    return x.r==y.r && x.eip==y.eip && x.flags==y.flags && x.defined_flags==y.defined_flags &&
        x.fs_base==y.fs_base && x.gs_base==y.gs_base && x.fs_valid==y.fs_valid &&
        x.gs_valid==y.gs_valid && x.selectors==y.selectors && x.fp.bytes==y.fp.bytes;
}
static Result stack_case(bool native,bool linear,unsigned variant,bool pop,bool nested){
    wr::Memory m,other;if(linear)CHECK(m.enable_linear_memory());
    const auto partial=variant==8;const unsigned length=partial?4099:8192;
    m.map(0x100000,length,3);m.map(0xfffff000u,4096,3);
    std::vector<std::uint8_t> initial(length);for(unsigned n=0;n<length;++n)initial[n]=std::uint8_t(n*37+19);
    m.copy_in(0x100000,initial);std::array<std::uint8_t,4096> high;high.fill(0x5a);m.copy_in(0xfffff000u,high);
    U32 at=0x100040;switch(variant){
    case 1:at=0x100041;break;case 2:at=0x100ffe;break;
    case 3:at=0x100ffe;m.protect(0x101000,4096,0);break;
    case 4:m.protect(0x100000,4096,wr::Memory::Read);break;
    case 5:m.protect(0x100000,4096,wr::Memory::Write);break;
    case 6:m.protect(0x100000,4096,7);break;
    case 7:at=0x200040;break;case 8:at=0x101000;break;
    case 9:at=0xfffffffcu;break;case 10:at=0xfffffffeu;break;
    case 11:at=0x100ffe;m.protect(0x101000,4096,wr::Memory::Read);break;
    case 12:at=0x100ffe;m.decommit(0x101000,4096);break;
    }
    Result result;for(unsigned i=0;i<8;++i)result.cpu.r[i]=0x98760000+i;
    result.cpu.r[wr::ESP]=pop?at:at+4;const auto before=result.cpu;
    {wr::Memory::ExecutionScope scope(m);std::optional<wr::Memory::ExecutionScope> inner;if(nested)inner.emplace(other);
        try{if(pop)result.value=native?a::text_pop(result.cpu,scope):wr::pop(result.cpu,m);
            else if(native)a::text_push(result.cpu,scope,0x89abcdef);else wr::push(result.cpu,m,0x89abcdef);
        }catch(const wr::GuestFault& f){result.fault={{f.kind,f.address}};result.message=f.what();}
    }
    result.epoch=m.code_epoch();if(result.fault)CHECK(same_cpu(result.cpu,before));
    // Inspect only still-committed owned bytes, after recording fault/epoch.
    const auto readable_length=variant==12?4096:length;
    m.protect(0x100000,readable_length,3);m.protect(0xfffff000u,4096,3);
    result.bytes.resize(readable_length+4096);m.copy_out(0x100000,{result.bytes.data(),readable_length});
    m.copy_out(0xfffff000u,{result.bytes.data()+readable_length,4096});return result;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned stack_cases{},fault_cases{},nested_cases{};
    for(bool linear:{false,true})for(unsigned variant=0;variant<13;++variant)for(bool pop:{false,true})for(bool nested:{false,true}){
        const auto expected=stack_case(false,linear,variant,pop,nested),actual=stack_case(true,linear,variant,pop,nested);
        CHECK(same_cpu(expected.cpu,actual.cpu) && expected.value==actual.value && expected.fault==actual.fault &&
            expected.message==actual.message && expected.bytes==actual.bytes && expected.epoch==actual.epoch);
        ++stack_cases;if(actual.fault)++fault_cases;if(nested)++nested_cases;
    }
    const auto before=a::text_jump_counts;
    {a::TextJumpBudget<> budget;for(unsigned n=0;n<128;++n)CHECK(budget.take());CHECK(!budget.take());budget.external();}
    auto after=a::text_jump_counts;CHECK(after.entries==before.entries+1 && after.local_jumps==before.local_jumps+128 &&
        after.budget_yields==before.budget_yields+1 && after.external_targets==before.external_targets+1);
    {a::TextJumpBudget<true> budget;CHECK(budget.take());}
    CHECK(a::text_jump_counts.entries==after.entries);
    {a::NativeCallScope generic(nullptr,nullptr);CHECK(a::current_native_calls->remaining==32);}

    // Change only a table word in a constructed Memory, then resume the raw
    // body at its JMP. Unknown targets must still reach the checked callback.
    // This deliberately bypasses immutable Program proofs only in this test.
    a::Runtime runtime;runtime.load();auto& p=runtime.process();auto cpu=p.cpu;
    const U32 unknown=0xdead1234;std::array<std::uint8_t,4> replacement;
    std::memcpy(replacement.data(),&unknown,4);p.memory.initialize(0x44788c,replacement);
    struct Capture {unsigned calls{};U32 target{};wr::Cpu cpu;};Capture captured;
    cpu.r[wr::ECX]=0;cpu.eip=0x446723;const auto location=a::locate(cpu.eip);CHECK(location.function && location.resume);
    {a::NativeCallScope service([](wr::Cpu& c,wr::Memory&,wr::Memory::ExecutionScope&,void* owner){
            auto& out=*static_cast<Capture*>(owner);++out.calls;out.target=c.eip;out.cpu=c;return false;
        },&captured);
        wr::Memory::ExecutionScope scope(p.memory);std::uint64_t unused{};
        a::f_446500<false,true>(cpu,p.memory,scope,unused,location.resume);
    }
    CHECK(captured.calls==1 && captured.target==unknown && same_cpu(cpu,captured.cpu));
    CHECK(a::text_jump_counts.external_targets==after.external_targets+1);
    std::ofstream report(argv[1]);report<<"{\"stack_cases\":"<<stack_cases<<",\"fault_cases\":"<<fault_cases<<",\"nested_cases\":"<<nested_cases
        <<",\"maximum_local_jumps\":128,\"checked_unknown_target\":true,\"precise_atomic_faults\":true,\"code_epoch_preserved\":true,\"paged_and_linear\":true,\"generic_call_limit_unchanged\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    CHECK(report);std::cout<<"Checked "<<stack_cases<<" stack cases and unknown text JMP delegation.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
