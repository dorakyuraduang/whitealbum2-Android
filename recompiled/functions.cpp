#include "functions.hpp"
#include <algorithm>
#include "work_samples.hpp"

namespace wa2::aot {
void Program::ensure(const Function& function){
    // Called only inside invoke's ExecutionScope. The full direct-call closure
    // is proven once here, so generated direct calls need no guards/dispatch.
    const auto epoch=memory_.code_epoch();
    if(epoch_!=epoch){std::fill(verified_.begin(),verified_.end(),false);epoch_=epoch;}
    const auto index=std::size_t(&function-functions().data());
    if(index>=verified_.size())throw std::logic_error("function is not in the shared static program");
    if(verified_[index])return;
    verified_[index]=1;
    for(const auto& span:function.code){
        // No guest code runs from these bytes. Read-only input is required so
        // a store cannot invalidate the proof halfway through a native body.
        for(std::uint64_t at=span.address;at<std::uint64_t(span.address)+span.bytes.size();){
            if(memory_.permissions(std::uint32_t(at))&wr::Memory::Write)
                throw wr::GuestFault(wr::FaultKind::unsupported,function.entry,"shared AOT requires immutable code pages");
            at=(at/4096+1)*4096;
        }
        if(!memory_.matches_code(span.address,span.bytes))
            throw wr::GuestFault(wr::FaultKind::unsupported,function.entry,"code differs from the shared static translation");
    }
    for(auto entry:function.callees){
        const auto* callee=find(entry);
        if(!callee)throw std::logic_error("missing statically bound callee");
        ensure(*callee);
    }
    verified_[index]=2;
}
void Program::prove(const Function& function){
    try{ensure(function);}catch(...){
        // A proven node in a recursive SCC may depend on an ancestor whose
        // later callee failed. Never retain a partial closure after failure.
        std::fill(verified_.begin(),verified_.end(),0);throw;
    }
}
bool Program::invoke(Context& context,std::uint64_t& budget){
    const auto location=locate(context.eip);const auto* function=location.function;if(!function)return false;
#if WA2_AOT_DIAGNOSTICS
    auto scope=memory_.execution_scope();prove(*function);
    function->body(context,memory_,scope,budget,true,location.resume);return true;
#else
    (void)budget;
    throw std::logic_error("instruction metering is available only in the diagnostic AOT build");
#endif
}
bool Program::invoke_unmetered(Context& context){
    const auto location=locate(context.eip);const auto* function=location.function;if(!function)return false;
    auto scope=memory_.execution_scope();prove(*function);
    struct Binding {Program* program;Context* context;} binding{this,&context};
    NativeCallScope native_calls{[](wr::Cpu& cpu,wr::Memory& memory,Scope& scope,void* owner){
        auto& binding=*static_cast<Binding*>(owner);
        if(&cpu!=binding.context || &memory!=&binding.program->memory_)return false;
#if WA2_AOT_DIAGNOSTICS
        if(!native_indirect_calls_enabled)return false;
#endif
        return binding.program->invoke_indirect(cpu,scope);
    },&binding};
    std::uint64_t unused{};
    FunctionWorkTimer timing{std::size_t(function-functions().data()),function->entry,context.eip,context.fp.get16(0)};
    function->body(context,memory_,scope,unused,false,location.resume);return true;
}
bool Program::invoke_indirect(Context& context,Scope& scope){
    const auto location=locate(context.eip);const auto* function=location.function;if(!function)return false;
    // Immutable-code proof and the exact source ABI are identical to an outer
    // entry. Reuse its RAM scope, native call stack and finite call allowance.
    prove(*function);std::uint64_t unused{};
    function->body(context,memory_,scope,unused,false,location.resume);return true;
}
}
