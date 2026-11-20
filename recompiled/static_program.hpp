#pragma once
#include "functions.hpp"
#include <algorithm>

namespace wa2::aot {
// A catalogue belongs to one statically translated program. Device adapters
// never select addresses or alter the compiled program's control flow.
class StaticProgram {
    wr::Memory& memory_;
    std::span<const Function> functions_;
    Location (*locate_)(std::uint32_t) noexcept;
    std::vector<std::uint8_t> verified_;
    std::uint64_t epoch_{~std::uint64_t{0}};

    void ensure(const Function& function) {
        const auto index=std::size_t(&function-functions_.data());
        if(index>=verified_.size())throw std::logic_error("function outside static catalogue");
        if(verified_[index])return;
        verified_[index]=1;
        for(const auto& span:function.code){
            for(std::uint64_t at=span.address;at<std::uint64_t(span.address)+span.bytes.size();at=(at/4096+1)*4096)
                if(memory_.permissions(std::uint32_t(at))&wr::Memory::Write)
                    throw wr::GuestFault(wr::FaultKind::unsupported,function.entry,"static AOT requires immutable code pages");
            if(!memory_.matches_code(span.address,span.bytes))
                throw wr::GuestFault(wr::FaultKind::unsupported,function.entry,"code differs from static translation");
        }
        for(const auto entry:function.callees){
            const auto location=locate_(entry);
            if(!location.function)throw std::logic_error("unresolved static direct callee");
            ensure(*location.function);
        }
        verified_[index]=2;
    }
    void prove(const Function& function) {
        const auto epoch=memory_.code_epoch();
        if(epoch_!=epoch){std::fill(verified_.begin(),verified_.end(),0);epoch_=epoch;}
        try{ensure(function);}catch(...){std::fill(verified_.begin(),verified_.end(),0);throw;}
    }
    struct Binding {
        StaticProgram* program; Context* context;
        static bool invoke(Context& cpu,wr::Memory& memory,Scope& scope,void* owner){
            auto& b=*static_cast<Binding*>(owner);
            if(&cpu!=b.context || &memory!=&b.program->memory_)return false;
            return b.program->invoke(cpu,scope);
        }
    };
    bool invoke(Context& cpu,Scope& scope) {
        const auto location=locate_(cpu.eip);
        if(!location.function)return false;
        prove(*location.function);
        std::uint64_t unused{};
        location.function->body(cpu,memory_,scope,unused,false,location.resume);
        return true;
    }
public:
    StaticProgram(wr::Memory& memory,std::span<const Function> functions,
                  Location (*locate)(std::uint32_t) noexcept):
        memory_(memory),functions_(functions),locate_(locate),verified_(functions.size()){}
    bool invoke(Context& cpu) {
        auto scope=memory_.execution_scope();
        Binding binding{this,&cpu};
        NativeCallScope calls{Binding::invoke,&binding};
        return invoke(cpu,scope);
    }
};
}
