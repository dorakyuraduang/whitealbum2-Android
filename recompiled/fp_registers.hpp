#pragma once
#include "fp_execution.hpp"
#include <utility>

namespace wa2::aot::fp {
#if WA2_AOT_DIAGNOSTICS
inline thread_local bool register_fp_enabled=true;
struct RegisterFpReferenceScope {
    bool previous{register_fp_enabled};
    RegisterFpReferenceScope(){register_fp_enabled=false;}
    ~RegisterFpReferenceScope(){register_fp_enabled=previous;}
};
#endif
namespace detail {
// The physical slots stay in place across the statically compiled arithmetic.
// TOP changes an index, rather than moving 128 bytes for each load/pop. The
// operation itself is shared with the independently checked serialized kernel.
class RegisterStack {
    std::array<Extended,8> physical_;
    std::uint8_t tags_{};
    unsigned initial_exceptions_{};
    bool underflow_{},overflow_{};
public:
    Environment environment;
    void load(const wr::FpState& state){
        environment={state.get16(0),state.get16(2)};tags_=state.bytes[4];
        for(unsigned n=0;n<8;++n)physical_[(top()+n)&7]={state.get64(32+n*16),state.get16(40+n*16)};
    }
    void save(wr::FpState& state)const{
        for(unsigned n=0;n<8;++n){const auto value=raw(n);state.put64(32+n*16,value.significand);state.put16(40+n*16,value.sign_exponent);}
        state.bytes[4]=tags_;state.put16(0,environment.control);state.put16(2,environment.status);
        state.clear_x87_padding();state.put32(28,0xffff);
    }
    void begin(){initial_exceptions_=environment.status&0x3f;underflow_=overflow_=false;}
    void finish(){
        if(underflow_ || overflow_)environment.status=std::uint16_t(
            (environment.status&~(0x3f|RoundUp))|initial_exceptions_|Invalid|0x40|(overflow_ && !underflow_?RoundUp:0));
    }
    unsigned top()const noexcept{return (environment.status>>11)&7;}
    bool present(unsigned index)const noexcept{return tags_&(1u<<((top()+index)&7));}
    Extended raw(unsigned index)const{return physical_[(top()+index)&7];}
    Extended get(unsigned index){
        if(present(index))return raw(index);
        underflow_=true;environment.raise(Invalid|0x40);environment.status&=~RoundUp;return indefinite();
    }
    void set(unsigned index,Extended value,bool canonical_fault=true){
        if(canonical_fault && (underflow_ || overflow_))value=indefinite();
        const auto slot=(top()+index)&7;physical_[slot]=value;tags_|=std::uint8_t(1u<<slot);
    }
    void push(Extended value){
        const auto next=(top()+7)&7;
        if(tags_&(1u<<next)){overflow_=true;environment.raise(Invalid|0x40|RoundUp);value=indefinite();}
        environment.status=std::uint16_t((environment.status&~0x3800)|(next<<11));set(0,value);
    }
    void pop(){
        tags_&=std::uint8_t(~(1u<<top()));const auto next=(top()+1)&7;
        environment.status=std::uint16_t((environment.status&~0x3800)|(next<<11));
    }
    void control(std::uint16_t value){
        value=std::uint16_t((value&0x1f3f)|0x40);environment.control=value;
        if(environment.status&~value&0x3f)environment.status|=0x8080;else environment.status&=~0x8080;
    }
    void compare_values(Extended left,Extended right,bool ordered){
        const auto result=fp::compare(left,right,environment,ordered);environment.status&=~0x4700;
        environment.status|=std::uint16_t(result==2?0x4500:result<0?0x100:result==0?0x4000:0);
    }
    void examine(){
        const auto value=raw(0);unsigned kind;
        if(!present(0))kind=5;else if(value.unsupported())kind=0;else if(value.nan())kind=1;
        else if(value.infinity())kind=3;else if(value.zero())kind=4;else if(!value.exponent())kind=6;else kind=2;
        environment.status&=~0x4700;environment.status|=std::uint16_t(
            (kind&1?0x100:0)|(kind&2?0x400:0)|(kind&4?0x4000:0)|(value.sign()?0x200:0));
    }
};
}

// One lazy register file per native function invocation. Import/direct native
// calls flush it first. Precise memory faults unwind it, committing only the
// completed prefix. Unmasked arithmetic keeps the original transactional ABI.
// Metered differential bodies always use that original ABI too.
template<bool Metered>
class RegisterFrame {
    wr::Cpu& cpu_;wr::Memory& memory_;wr::Memory::ExecutionScope& scope_;
    detail::RegisterStack stack_;
    bool active_{};std::uint64_t instruction_{},data_{};std::uint16_t opcode_{};
    bool enabled()const{
        if constexpr(Metered)return false;
#if WA2_AOT_DIAGNOSTICS
        return register_fp_enabled;
#else
        return true;
#endif
    }
    void load(){
        if(!active_){stack_.load(cpu_.fp);instruction_=cpu_.fp.get64(8);data_=cpu_.fp.get64(16);opcode_=cpu_.fp.get16(6);active_=true;}
    }
    void wait()const{
        if(active_){if((stack_.environment.status&~stack_.environment.control)&0x3f)
            throw wr::GuestFault(wr::FaultKind::floating,cpu_.eip,"pending guest x87 exception; SEH delivery is not implemented");}
        else wr::x87_wait(cpu_);
    }
public:
    RegisterFrame(wr::Cpu& cpu,wr::Memory& memory,wr::Memory::ExecutionScope& scope):cpu_(cpu),memory_(memory),scope_(scope){}
    ~RegisterFrame(){flush();}
    void flush(){if(active_){stack_.save(cpu_.fp);cpu_.fp.put64(8,instruction_);cpu_.fp.put64(16,data_);cpu_.fp.put16(6,opcode_);active_=false;}}
    std::uint16_t word(unsigned offset)const{
        return active_?(offset==0?stack_.environment.control:stack_.environment.status):cpu_.fp.get16(offset);
    }
    template<class Function> decltype(auto) external(Function&& function){flush();return std::forward<Function>(function)();}
    template<unsigned Opcode,unsigned Encoding,bool Memory>
    void execute(void* operand,unsigned opcode,bool control=false,bool integer_flags=false,wr::U32 data_address=0,bool has_memory=false){
        if(!enabled() || control || (word(0)&0x3f)!=0x3f){
            flush();fp::x87_execute<fp::x87<Opcode,Encoding,Memory>>(cpu_,operand,opcode,control,integer_flags,data_address,has_memory);return;
        }
        load();stack_.begin();x87_operation<Opcode,Encoding,Memory>(stack_,operand);stack_.finish();
        instruction_=cpu_.eip;opcode_=std::uint16_t(opcode&0x7ff);if(has_memory)data_=data_address;
        if(integer_flags)cpu_.defined_flags|=wr::STATUS_FLAGS;
    }
    template<unsigned Opcode,unsigned Encoding,bool Memory>
    void memory(wr::U32 address,unsigned size,bool store,unsigned opcode,bool control=false){
        if(!enabled()){
            fp::x87_memory<fp::x87<Opcode,Encoding,Memory>>(cpu_,memory_,address,size,store,opcode,control);return;
        }
        wait();if(!size || size>10)throw wr::GuestFault(wr::FaultKind::unsupported,cpu_.eip,"unsupported native x87 operand size");
        alignas(16) std::array<std::uint8_t,16> operand{};
        if(store)detail::check_store_operand(memory_,scope_,address,size);
        else detail::read_operand(memory_,scope_,address,operand.data(),size);
        execute<Opcode,Encoding,Memory>(operand.data(),opcode,control,false,address,true);
        if(store)scope_.write_bytes(address,std::span(operand.data(),size));
    }
};
}
