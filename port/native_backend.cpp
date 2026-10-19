#include "native_backend.hpp"
#include "static_blocks.hpp"
#include "linear_faults.hpp"
#include "native_state_apis.hpp"
#include <cstddef>
#include <cstring>
#include <sstream>
#include <map>
namespace wr {
#if defined(WA2_COMPACT_FALLBACK)
bool wa2_compact_verify_page(Memory&,U32);
#endif
namespace {
struct alignas(16) NativeState {
    std::array<U32,8> registers;
    U32 pc,flags,defined,fp_pc{},fp_data{},fp_opcode{},fp_active{},lazy_flags{};
    FpState fp;
    const std::uint8_t* verified_pages{};
    U32 return_page{},flags_af{},sse_changed{},saved_eax{},saved_status{},control_word{},control_changed{},repeat_ticks{},direction{},xmm_changed{};
    const Wa2StateApiBinding* state_apis{};
    std::uint64_t* api_calls{};
    U32 inline_api_ticks{},repeat_calls{};
    std::uint64_t total_budget{};
};
static_assert(offsetof(NativeState,pc)==32 && offsetof(NativeState,fp_opcode)==52 && offsetof(NativeState,lazy_flags)==60 && offsetof(NativeState,fp)==64);
static_assert(offsetof(NativeState,verified_pages)==576 && offsetof(NativeState,return_page)==584);
static_assert(offsetof(NativeState,flags_af)==588 && offsetof(NativeState,sse_changed)==592 && offsetof(NativeState,saved_eax)==596 && offsetof(NativeState,saved_status)==600);
static_assert(offsetof(NativeState,control_word)==604 && offsetof(NativeState,control_changed)==608);
static_assert(offsetof(NativeState,repeat_ticks)==612 && offsetof(NativeState,direction)==616);
static_assert(offsetof(NativeState,xmm_changed)==620);
static_assert(offsetof(NativeState,state_apis)==624 && offsetof(NativeState,api_calls)==632 && offsetof(NativeState,inline_api_ticks)==640);
static_assert(offsetof(NativeState,repeat_calls)==644 && offsetof(NativeState,total_budget)==648);
static_assert(sizeof(Wa2StateApiBinding)==48 && offsetof(Wa2StateApiBinding,columns)==8 && offsetof(Wa2StateApiBinding,rows)==12 &&
    offsetof(Wa2StateApiBinding,receiver)==16 && offsetof(Wa2StateApiBinding,kind)==20 && offsetof(Wa2StateApiBinding,calls)==24 && offsetof(Wa2StateApiBinding,version)==32 && offsetof(Wa2StateApiBinding,baseline)==40);
static_assert(offsetof(Memory::NativePage,data)==0 && offsetof(Memory::NativePage,permissions)==8 && offsetof(Memory::NativePage,length)==12);
struct NativePageCode {U32 page;bool(*verify)(Memory&);};
#if defined(WA2_COMPACT_FALLBACK)
#define WA2_NATIVE_VERIFY_PAGE(memory,pc,fallback) wa2_compact_verify_page(memory,pc)
#else
#define WA2_NATIVE_VERIFY_PAGE(memory,pc,fallback) fallback
#endif
#include "native_backend_data.inc"
#undef WA2_NATIVE_VERIFY_PAGE
extern "C" const std::uint8_t wa2_native_entry_lengths[];
extern "C" const std::uint16_t wa2_native_entry_indices[];
thread_local std::uint64_t attempts{},batches{},instructions{},fpu_batches{},linear_batches{},linear_instructions{};
thread_local std::uint64_t minimum_budget_rejections{};
thread_local bool profile_enabled{};
thread_local unsigned profile_countdown=4093;
thread_local std::map<U32,std::array<std::uint64_t,3>> profile_exits;
thread_local std::map<Memory*,std::weak_ptr<Wa2StateApiTable>> state_tables;
thread_local std::uint64_t inline_apis{};
thread_local std::uint64_t repeat_calls{},repeat_iterations{};
std::span<const std::uint8_t> verified_code_pages(Memory& memory){
    return memory.native_code_proofs(&native_code_pages,0xb0,[&](std::span<std::uint8_t> pages){
        for(const auto& code:native_code_pages)try{pages[(code.page>>12)-0x400]=code.verify(memory);}catch(const GuestFault&){}
    });
}
}
std::shared_ptr<Wa2StateApiTable> wa2_state_apis(Memory& memory,std::uint64_t* api_calls){
    auto table=std::make_shared<Wa2StateApiTable>();table->api_calls=api_calls;state_tables[&memory]=table;return table;
}
void wa2_bind_state_api(Wa2StateApiTable& table,U32 address,U32 receiver,Wa2StateApiKind kind,U32* values,U32 rows,U32 columns,std::uint64_t* calls,std::uint64_t* version,const U32* baseline){
    const auto offset=address-0xf0000000u;if(offset>=0x10000u || (offset&15u) || !receiver || !values || !calls)return;
    table.entries[offset>>4]={values,columns,rows,receiver,U32(kind),calls,version,baseline};
}
void wa2_unbind_state_receiver(Wa2StateApiTable& table,U32 receiver){for(auto& entry:table.entries)if(entry.receiver==receiver)entry={};}
void wa2_unbind_state_api(Wa2StateApiTable& table,U32 address){const auto offset=address-0xf0000000u;if(offset<0x10000u && !(offset&15u))table.entries[offset>>4]={};}
void wa2_native_profiling(bool enabled){profile_enabled=enabled;profile_countdown=4093;profile_exits.clear();}
std::uint64_t wa2_native_inline_state_api_calls(){return inline_apis;}
std::span<const U32> wa2_native_entries(){return native_entries;}
bool wa2_native_entry(U32 pc){const auto offset=pc-0x400000;return offset<0xb0000 && wa2_native_entry_indices[offset];}
bool wa2_native_code_verified(Memory& memory,U32 pc){const auto offset=pc-0x400000;return offset<0xb0000 && verified_code_pages(memory)[offset>>12];}
unsigned wa2_native_batch(Cpu& s,Memory& m,std::uint64_t& budget,unsigned limit,U32 return_pc){
    // Original 32-bit x87 instruction/data pointers and FOP fit this range.
    // Arbitrary restored/noncanonical metadata must retain the oracle's exact
    // FXSAVE normalization order, so it uses the checked fallback instead.
    const auto offset=s.eip-0x400000;
    if(!limit || !budget || offset>=0xb0000)return 0;
    const auto index=wa2_native_entry_indices[offset];if(!index)return 0;
    const auto minimum=wa2_native_entry_lengths[index];
    if(limit<minimum || budget<minimum){++minimum_budget_rejections;return 0;}
    std::uint16_t control,opcode;U32 mxcsr;std::uint64_t fp_pc,fp_data;
    // These fixed FXSAVE fields always fit the 512-byte object. Read each
    // once with unaligned-safe scalar loads instead of repeated byte getters.
    std::memcpy(&control,s.fp.bytes.data(),sizeof(control));
    std::memcpy(&opcode,s.fp.bytes.data()+6,sizeof(opcode));
    std::memcpy(&fp_pc,s.fp.bytes.data()+8,sizeof(fp_pc));
    std::memcpy(&fp_data,s.fp.bytes.data()+16,sizeof(fp_data));
    std::memcpy(&mxcsr,s.fp.bytes.data()+24,sizeof(mxcsr));
    if((s.eip>>12)==(return_pc>>12) || (control&0xe0ffu)!=0x7fu ||
       (mxcsr&0xffff1f80u)!=0x1f80u || fp_pc>0xffffffffull || fp_data>0xffffffffull || (opcode&0xf800u))return 0;
    ++attempts;
    auto execution=m.execution_scope();
    const auto* linear=execution.linear_base();const bool direct=linear && wa2_linear_available();
#if !defined(WA2_PAGED_NATIVE_BACKEND)
    // Unusual memory layouts retain the compiled C++ instruction path. Do
    // not ship another complete copy of the native program for this fallback.
    if(!direct)return 0;
#endif
    const auto verified=verified_code_pages(m);
    if(!verified[(s.eip>>12)-0x400])return 0;
    NativeState state;state.registers=s.r;state.pc=s.eip;state.flags=s.flags&STATUS_FLAGS;
    state.defined=s.defined_flags&STATUS_FLAGS;state.fp=s.fp;state.fp_data=U32(fp_data);
    state.verified_pages=verified.data();state.return_page=(return_pc>>12)-0x400;state.flags_af=s.flags;
    state.control_word=control;
    state.direction=s.flags&DF;
    state.total_budget=budget;
    const auto binding=state_tables.find(&m);
    const auto state_apis=binding!=state_tables.end()?binding->second.lock():nullptr;
    if(state_apis){state.state_apis=state_apis->entries.data();state.api_calls=state_apis->api_calls;}
    const auto maximum=unsigned(std::min<std::uint64_t>(limit,budget));
#if defined(WA2_PAGED_NATIVE_BACKEND)
    const auto count=direct?wa2_linear_call(&state,linear,maximum):wa2_native_run(&state,execution.native_pages(),maximum);
#else
    const auto count=wa2_linear_call(&state,linear,maximum);
#endif
    repeat_calls+=state.repeat_calls;repeat_iterations+=state.repeat_ticks;
    if(profile_enabled && !--profile_countdown){profile_countdown=4093;auto& sample=profile_exits[state.pc];++sample[0];sample[1]+=count;sample[2]+=state.fp_active!=0;}
    fpu_batches+=state.fp_active!=0;
    if(!count && !state.repeat_ticks && !state.inline_api_ticks)return 0;
    s.r=state.registers;s.eip=state.pc;
    s.flags=(s.flags&~STATUS_FLAGS)|(state.flags&STATUS_FLAGS);
    s.defined_flags=(s.defined_flags&~STATUS_FLAGS)|state.defined;
    if(state.fp_pc || state.sse_changed || state.control_changed){
        state.fp.put16(0,std::uint16_t(state.control_word));
        if(state.fp_pc){state.fp.put64(8,state.fp_pc);
            state.fp.put64(16,state.fp_data);state.fp.put16(6,std::uint16_t(state.fp_opcode));}
        else{state.fp.put64(8,s.fp.get64(8));state.fp.put64(16,s.fp.get64(16));if(!state.sse_changed)state.fp.put16(6,s.fp.get16(6));}
        s.fp=state.fp;
    }
    else if(state.xmm_changed)std::copy_n(state.fp.bytes.data()+160,128,s.fp.bytes.data()+160);
    budget-=count+state.repeat_ticks+state.inline_api_ticks;inline_apis+=state.inline_api_ticks;
    ++batches;instructions+=count;if(direct){++linear_batches;linear_instructions+=count;}return count;
}
std::string wa2_native_report(){std::ostringstream out;out<<"{\"backend\":\"native-register-blocks\",\"attempts\":"<<attempts<<",\"batches\":"<<batches<<",\"guest_instructions\":"<<instructions<<",\"fpu_context_loads\":"<<fpu_batches<<",\"linear_batches\":"<<linear_batches<<",\"linear_instructions\":"<<linear_instructions<<",\"memory_fault_recoveries\":"<<wa2_linear_fault_count()<<",\"cpp_helpers_inside_blocks\":false";
    out<<",\"inline_state_api_calls\":"<<inline_apis;
    out<<",\"native_string_calls\":"<<repeat_calls<<",\"native_string_iterations\":"<<repeat_iterations;
    out<<",\"minimum_budget_rejections\":"<<minimum_budget_rejections;
    const auto layout=wa2_linear_code_layout();
    out<<",\"code_layout\":{\"base\":"<<layout.begin<<",\"end\":"<<layout.end<<",\"points\":"<<layout.points<<",\"point_count\":"<<layout.count<<",\"stride\":8}";
    if(profile_enabled){out<<",\"exit_sample_interval\":4093,\"sampled_exits\":[";bool first=true;for(const auto& [pc,sample]:profile_exits){if(!first)out<<',';first=false;out<<"{\"pc\":"<<pc<<",\"samples\":"<<sample[0]<<",\"instructions\":"<<sample[1]<<",\"fpu_loads\":"<<sample[2]<<'}';}out<<']';}out<<'}';return out.str();}
}
