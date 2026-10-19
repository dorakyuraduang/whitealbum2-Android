#include "winrecomp/process.hpp"
#include "winrecomp/hash.hpp"
#include "winrecomp/win32_state.hpp"
#include "winrecomp/win64_abi.hpp"
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
namespace wr {
namespace {
std::string canonical(std::string s) {for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
U32 page_size(U32 size) {
    const auto n=(std::uint64_t(size)+4095)&~4095ull;
    if(!size || n>512ull*1024*1024)throw std::runtime_error("invalid guest allocation size");return U32(n);
}
unsigned protection(U32 value) {
    switch(value){case 0x01:return 0;case 0x02:return Memory::Read;case 0x04:return Memory::Read|Memory::Write;
    case 0x10:return Memory::Execute;case 0x20:return Memory::Execute|Memory::Read;case 0x40:return Memory::Execute|Memory::Read|Memory::Write;
    default:throw std::runtime_error("unsupported page protection (guard/copy-on-write/cache modifiers not implemented)");}
}
U32 native_protection(unsigned value){switch(value){case 0:return 1;case 1:return 2;case 3:return 4;case 4:return 0x10;case 5:return 0x20;case 7:return 0x40;default:throw std::runtime_error("invalid guest protection state");}}
constexpr U32 PROCESS_RETURN=0xffffffe0,CALLBACK_RETURN=0xffffffd0;
struct WallSample {
    std::uint64_t* total{};std::uint64_t* sample{};std::uint64_t* maximum{};std::chrono::steady_clock::time_point start;
    explicit WallSample(std::uint64_t* destination,std::uint64_t* sampled=nullptr,std::uint64_t* max=nullptr):total(destination),sample(sampled),maximum(max){if(total || sample)start=std::chrono::steady_clock::now();}
    ~WallSample(){if(total || sample){const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());if(total)*total+=ns;if(sample)*sample+=ns;if(maximum)*maximum=std::max(*maximum,ns);}}
};
}
#ifndef WA2_STATIC_FUNCTIONS_ONLY
Process::Process(StepFunction step,ProcessOptions opts):win32_(std::make_unique<Win32State>()),step_(step),options(std::move(opts)),budget(options.instruction_budget) {
    if(!step_)throw std::runtime_error("missing compiled dispatch function");
    initialize({install_gui,install_directdraw});
}
#endif
Process::Process(FunctionExecutor executor,ProcessOptions opts,ProcessBackends backends):
    win32_(std::make_unique<Win32State>()),functions_(std::move(executor)),options(std::move(opts)),budget(options.instruction_budget){
    if(!functions_.invoke)throw std::runtime_error("missing static function executor");
    if(options.prefer_linear_memory)memory.enable_linear_memory();
    initialize(std::move(backends));state().main_poll_interval=1;
}
void Process::initialize(ProcessBackends backends){
    if(!budget)throw std::runtime_error("instruction budget must be nonzero");
    options.data_root=std::filesystem::canonical(options.data_root);
    install_win32(*this);
    if(backends.gui)gui_=backends.gui(*this);
    if(backends.directdraw)directdraw_=backends.directdraw(*this);
    state().execute_slice=[this](U32 return_pc,unsigned count){
        unsigned remaining=function_execution()?std::min(count,16u):count;
        while(remaining && !exited_ && !state().yield_requested && cpu.eip!=return_pc){
            if(const auto n=execute_instructions(return_pc,remaining,"untranslated worker instruction")){remaining-=n;continue;}
            --remaining;if(!function_execution())tick(budget,cpu.eip);if(dispatch_api_boundary())continue;
            if(function_execution())throw GuestFault(FaultKind::untranslated,cpu.eip,"missing static worker function or host service");
            if(!step_(cpu,memory,budget))throw GuestFault(FaultKind::untranslated,cpu.eip,"untranslated worker instruction");++executed_instructions;
        }
    };
}
Process::~Process(){
    // Dependants are installed after their services: media must stop its
    // decoder before audio closes, and audio must stop before scheduler events
    // disappear. Window adapters outlive all render contexts.
    for(auto it=state().shutdown.rbegin();it!=state().shutdown.rend();++it)try{(*it)();}catch(...){}
    if(directdraw_)directdraw_->shutdown();if(gui_)gui_->shutdown();
}
const Image& Process::source_image() const {if(!source_image_)throw std::runtime_error("no loaded source image");return *source_image_;}
Win32State& Process::state(){return *win32_;}
void Process::load(const Image& image) {
    if(loaded_)throw std::runtime_error("process already has an image");
    if(image.base%4096 || image.size%4096)throw std::runtime_error("runtime requires page-aligned image base and size");
    if(image.directories[13].rva)throw std::runtime_error("delay imports not supported by this loader");
    if(image.directories[14].rva)throw std::runtime_error("managed PE runtime not supported");
    if(image.directories[9].rva)throw std::runtime_error("static PE TLS loading is not yet implemented; refusing partial initialization");
    // Image bytes, .bss, gaps and header protection are explicit. The full image
    // is reserved first; page permissions are the union of resident sections.
    memory.map(image.base,image.size,0);auto mapped=image.mapped();memory.initialize(image.base,mapped);
    std::vector<unsigned> permissions(image.size/4096,0);
    for(std::uint64_t p=0;p<image.headers_size;p+=4096)permissions.at(std::size_t(p/4096))|=Memory::Read;
    for(const auto& section:image.sections)if(section.span()) {
        const unsigned access=((section.characteristics&0x40000000u)?Memory::Read:0u)|((section.characteristics&0x80000000u)?Memory::Write:0u)|((section.characteristics&0x20000000u)?Memory::Execute:0u);
        for(std::uint64_t p=section.rva/4096;p<=(std::uint64_t(section.rva)+section.span()-1)/4096;++p)permissions.at(std::size_t(p))|=access;
    }
    for(std::size_t n=0;n<permissions.size();++n)memory.protect(image.base+U32(n*4096),4096,permissions[n]);
    image_base=image.base;image_size=image.size;entry=image.entry;
    input_sha256=function_execution() && !image.static_input_identity.empty()?image.static_input_identity:sha256(image.bytes);
    if(!options.stack_size || options.stack_base%4096 || options.stack_size%4096 || std::uint64_t(options.stack_base)+options.stack_size>0x100000000ull)throw std::runtime_error("invalid guest stack");
    memory.map(options.stack_base,options.stack_size,Memory::Read|Memory::Write);
    memory.protect(options.stack_base,4096,0); // fixed guard; no silent stack growth
    if(options.teb%4096)throw std::runtime_error("TEB must be page aligned");
    memory.map(options.teb,4096,Memory::Read|Memory::Write);cpu.fs_base=options.teb;cpu.fs_valid=true;
    memory.store(options.teb,0xffffffffu,32);memory.store(options.teb+4,options.stack_base+options.stack_size,32);
    memory.store(options.teb+8,options.stack_base+4096,32);memory.store(options.teb+0x18,options.teb,32);
    memory.store(options.teb+0x20,1,32);memory.store(options.teb+0x24,1,32);
    cpu.eip=entry;cpu.r[ESP]=options.stack_base+options.stack_size-16;push(cpu,memory,PROCESS_RETURN);
    for(const auto& item:image.imports) {
        module(item.dll,true);const std::string name=item.ordinal?"#"+std::to_string(*item.ordinal):item.name;
        const auto thunk=resolve(item.dll,name,true);
        const std::array<std::uint8_t,4> bytes{std::uint8_t(thunk),std::uint8_t(thunk>>8),std::uint8_t(thunk>>16),std::uint8_t(thunk>>24)};
        memory.initialize(item.iat,bytes);
    }
    source_image_=std::make_unique<Image>(image);
    loaded_=true;set_error(0);
}
void Process::register_api(const std::string& dll,const std::string& name,unsigned arguments,std::function<U32(Process&)> fn,bool caller_cleans_stack) {
    if(arguments>64 || !fn)throw std::runtime_error("invalid API registration");
    const auto key=canonical(dll)+"!"+name;
    if(api_names_.contains(key))throw std::runtime_error("duplicate API registration: "+key);
    if(next_thunk_>=0xf1000000)throw std::runtime_error("thunk space exhausted");
    const auto at=next_thunk_;next_thunk_+=16;api_names_[key]=at;apis_.push_back(std::make_unique<Api>(Api{canonical(dll),name,arguments,caller_cleans_stack,std::move(fn),key}));
}
void Process::replace_api(const std::string& dll,const std::string& name,unsigned arguments,std::function<U32(Process&)> fn,bool caller_cleans_stack){
    if(loaded_)throw std::logic_error("system adapters must bind before the process image is loaded");
    if(arguments>64 || !fn)throw std::runtime_error("invalid API replacement");
    const auto found=api_names_.find(canonical(dll)+"!"+name);
    if(found==api_names_.end()){register_api(dll,name,arguments,std::move(fn),caller_cleans_stack);return;}
    auto& api=*apis_.at((found->second-0xf0000000u)/16);
    if(api.arguments!=arguments || api.caller_cleans_stack!=caller_cleans_stack)
        throw std::runtime_error("system adapter changed an existing source API ABI: "+name);
    api.function=std::move(fn);api.leaf=false;
}
void Process::certify_leaf_api(const std::string& dll,const std::string& name){
    if(loaded_)throw std::logic_error("leaf certification must precede process load");
    const auto found=api_names_.find(canonical(dll)+"!"+name);
    if(found==api_names_.end())throw std::logic_error("missing leaf API: "+name);
    auto& api=*apis_.at((found->second-0xf0000000u)/16);
    if(!api.function)throw std::logic_error("unimplemented leaf API: "+name);
    api.leaf=true;
}
bool Process::dispatch_leaf_api(){
    if(state().yield_requested || state().api_retry || state().api_transfer || exited_)return false;
    if(cpu.eip<0xf0000000u || cpu.eip>=next_thunk_)return false;
    const auto offset=cpu.eip-0xf0000000u;
    if((offset&15u) || (offset>>4)>=apis_.size() || !apis_[offset>>4]->leaf)return false;
    const auto result=dispatch_api();
    if(state().api_retry || state().yield_requested || state().api_transfer || exited_)
        throw GuestFault(FaultKind::unsupported,cpu.eip,"certified leaf API violated its execution contract");
    if(result)++direct_leaf_api_calls;return result;
}
U32 Process::resolve(const std::string& dll,const std::string& name,bool allow_unimplemented) {
    const auto key=canonical(dll)+"!"+name;auto found=api_names_.find(key);
    if(found!=api_names_.end())return found->second;
    if(!allow_unimplemented){set_error(127);return 0;}
    if(next_thunk_>=0xf1000000)throw std::runtime_error("thunk space exhausted");
    const auto at=next_thunk_;next_thunk_+=16;api_names_[key]=at;apis_.push_back(std::make_unique<Api>(Api{canonical(dll),name,0,false,{},key}));return at;
}
U32 Process::module(const std::string& dll,bool create) {
    auto key=canonical(dll);if(key.empty())return image_base;
    const auto path=key.find_last_of("/\\");if(path!=std::string::npos)key=key.substr(path+1);
    if(key.find('.')==std::string::npos)key+=".dll";
    if(auto it=modules_.find(key);it!=modules_.end())return it->second;
    if(!create){set_error(126);return 0;}
    const auto value=next_module_;next_module_+=0x10000;modules_[key]=value;return value;
}
std::string Process::module_name(U32 handle) const {for(const auto& [name,value]:modules_)if(value==handle)return name;return {};}
U32 Process::argument(unsigned index) {
    const auto at=std::uint64_t(cpu.r[ESP])+4+std::uint64_t(index)*4;
    if(at+4>0x100000000ull)throw GuestFault(FaultKind::memory,cpu.r[ESP],"API argument stack wraps");return memory.load(U32(at),32);
}
void Process::register_native_function(U32 address,unsigned arguments,
    std::function<std::optional<U32>(Process&)> fn,bool caller_cleans_stack) {
    const auto offset=address-state().fast_base;
    if(arguments>64 || !fn || (!function_execution() && offset>=state().fast_entries.size()) || !address || native_functions_.contains(address))
        throw std::runtime_error("invalid native engine function registration");
    native_functions_.emplace(address,NativeFunction{arguments,caller_cleans_stack,std::move(fn)});
    if(!function_execution())state().fast_entries[offset]|=4;
}
bool Process::dispatch_native_function() {
    const auto found=native_functions_.find(cpu.eip);if(found==native_functions_.end())return false;
    const auto& function=found->second;
    const auto stack=cpu.r[ESP],pc=cpu.eip;const auto return_address=memory.load(stack,32);
    memory.check(stack,(function.arguments+1)*4,Memory::Read);
    const auto result=function.function(*this);
    if(!result)return false;
    if(cpu.r[ESP]!=stack || cpu.eip!=pc)
        throw GuestFault(FaultKind::unsupported,pc,"native engine function corrupted ABI state");
    cpu.r[EAX]=*result;cpu.r[ESP]+=4+(function.caller_cleans_stack?0:function.arguments*4);
    cpu.eip=return_address;return true;
}
bool Process::dispatch_api_boundary(){WallSample timing(function_execution()?&host_work.api_wall_ns:nullptr);return dispatch_api();}
void Process::poll_services(){WallSample timing(function_execution()?&host_work.poll_wall_ns:nullptr);for(auto& poll:state().poll)poll();}
bool Process::dispatch_api() {
    if(options.profile_execution && !--profile_countdown_){profile_countdown_=4093;++profile_samples_[cpu.eip];}
    // Every API thunk is allocated from this range. Ordinary guest PCs need
    // no tree lookup; unresolved addresses still reach the compiled dispatcher.
    if(cpu.eip<0xf0000000u || cpu.eip>=next_thunk_)return dispatch_native_function();
    const auto offset=cpu.eip-0xf0000000u;
    if((offset&15u) || (offset>>4)>=apis_.size())return false;
    auto& api=*apis_[offset>>4];
    U32 return_address;std::array<U32,64> argument_values;
    {
        auto read_scope=memory.execution_scope();
        const auto bytes=read_scope.readable(cpu.r[ESP],(api.arguments+1)*4);
        ++api_stack_snapshots;
        if(bytes.size()==(api.arguments+1)*4){
            // All supported hosts are little endian. Keep the portable byte
            // assembly path for a host with another native byte order.
            if constexpr(std::endian::native==std::endian::little){
                std::memcpy(&return_address,bytes.data(),4);
                std::memcpy(argument_values.data(),bytes.data()+4,api.arguments*4);
            }else{
                const auto word=[&](unsigned offset){const auto* v=bytes.data()+offset;return U32(v[0])|(U32(v[1])<<8)|(U32(v[2])<<16)|(U32(v[3])<<24);};
                return_address=word(0);for(unsigned n=0;n<api.arguments;++n)argument_values[n]=word(n*4+4);
            }
        }else{
            ++api_stack_slow_snapshots;
            // Preserve the original fault address/order on every rejected
            // span, including a wrapping stack or an unreadable next page.
            return_address=memory.load(cpu.r[ESP],32);
            for(unsigned n=0;n<api.arguments;++n)argument_values[n]=argument(n);
            memory.check(cpu.r[ESP],(api.arguments+1)*4,Memory::Read);
        }
    }
    const auto arguments=std::span(argument_values).first(api.arguments);
    std::string detail;
    if(!arguments.empty() && (api.name=="CreateFileA" || api.name=="FindFirstFileA" || api.name=="CreateDirectoryA" || api.name=="CreateMutexA")) {
        if(api.name=="CreateMutexA" && arguments.size()<3)detail="<unreadable bounded guest name>";
        else try{const auto pointer=api.name=="CreateMutexA"?arguments[2]:arguments[0];if(pointer)detail=read_string(pointer,1024);}catch(const std::exception&){detail="<unreadable bounded guest name>";}
    }
    auto& transfer=recent_transfers.append();
    transfer.pc=cpu.eip;transfer.return_address=return_address;transfer.api=api.trace_name;
    std::copy(arguments.begin(),arguments.end(),transfer.arguments.begin());transfer.argument_count=api.arguments;transfer.detail=std::move(detail);
    if(!api.function)throw GuestFault(FaultKind::unsupported,cpu.eip,"unimplemented Win32 import "+api.dll+"!"+api.name);
    struct ArgumentScope {
        std::span<const U32>& destination;U32& stack;U32& pc;
        std::span<const U32> previous;U32 previous_stack,previous_pc;
        ~ArgumentScope(){destination=previous;stack=previous_stack;pc=previous_pc;}
    } argument_scope{api_arguments_,api_argument_stack_,api_argument_pc_,api_arguments_,api_argument_stack_,api_argument_pc_};
    api_arguments_=arguments;api_argument_stack_=cpu.r[ESP];api_argument_pc_=cpu.eip;
    const auto stack=cpu.r[ESP];const auto previous_yield=state().yield_requested;++api_calls;++api.calls;const bool sampled=api.calls==1 || !(api.calls%127);if(sampled)++api.timed_calls;
    WallSample timing(options.profile_execution?&profile_api_ns_[api.trace_name]:nullptr,sampled?&api.sampled_ns:nullptr,sampled?&api.max_sample_ns:nullptr);const auto value=api.function(*this);
    if(api.leaf && (state().api_retry || state().yield_requested!=previous_yield || state().api_transfer || exited_))
        throw GuestFault(FaultKind::unsupported,cpu.eip,"certified leaf API violated its execution contract");
    if(state().api_retry){
        if(!state().yield_requested || cpu.r[ESP]!=stack)throw GuestFault(FaultKind::unsupported,cpu.eip,"invalid suspended API state");
        state().api_retry=false;return true;
    }
    if(state().api_transfer){cpu=*state().api_transfer;state().api_transfer.reset();return true;}
    if(cpu.r[ESP]!=stack)throw GuestFault(FaultKind::unsupported,cpu.eip,"host API corrupted guest stack state");
    cpu.r[EAX]=value;cpu.r[ESP]+=4+(api.caller_cleans_stack?0:api.arguments*4);cpu.eip=return_address;return true;
}
unsigned Process::execute_instructions(U32 return_pc,unsigned count,const char* context) {
    if(function_execution()){
        if(!count || exited_ || cpu.eip==return_pc || native_functions_.contains(cpu.eip) ||
           (cpu.eip>=0xf0000000u && cpu.eip<next_thunk_))return 0;
        // invoke owns and RELEASES its memory scope before this returns. The
        // next scheduler/API boundary never runs beneath a native call stack.
        WallSample timing(&host_work.function_wall_ns,options.profile_execution?&profile_wall_ns_[cpu.eip&~0xfffu]:nullptr);
        if(!functions_.invoke(cpu,memory))return 0;
        ++native_function_entries;return 1;
    }
    // Never hold the memory execution lock across an API, wait or scheduler
    // poll. The same compiled instruction function and precise fault boundary
    // are used; only repeated locking and ordinary-PC API lookups are elided.
    if(cpu.eip>=0xf0000000u && cpu.eip<next_thunk_)return 0;
    const auto native_offset=cpu.eip-state().fast_base;
    if(native_offset<state().fast_entries.size() && (state().fast_entries[native_offset]&4u))return 0;
    WallSample timing(options.profile_execution?&profile_wall_ns_[cpu.eip&~0xfffu]:nullptr);
    auto scope=memory.execution_scope();unsigned executed=0;
    while(executed<count && !exited_ && cpu.eip!=return_pc && !(cpu.eip>=0xf0000000u && cpu.eip<next_thunk_)){
        const auto fast_offset=cpu.eip-state().fast_base;
        const auto entry_kind=fast_offset<state().fast_entries.size()?state().fast_entries[fast_offset]:0;
        if(entry_kind&4u)break;
        if((entry_kind&1u) && state().fast_step){
            const auto limit=options.profile_execution?std::min(count-executed,profile_countdown_-1):count-executed;
            auto* profile=options.profile_execution?&state().fast_profile[cpu.eip]:nullptr;
            if(profile){++(*profile)[0];(*profile)[3]=cpu.fp.get16(0);(*profile)[4]=limit;}
            if(const auto n=state().fast_step(cpu,memory,budget,limit,return_pc)){
                if(profile){++(*profile)[1];(*profile)[2]+=n;}
                if(n>limit)throw std::runtime_error("static instruction batch exceeded execution slice");
                if(options.profile_execution)profile_countdown_-=n;
                executed_instructions+=n;executed+=n;++state().fast_batches;state().fast_instructions+=n;continue;
            }
        }
        if((entry_kind&2u) && state().block_step){
            const auto limit=options.profile_execution?std::min(count-executed,profile_countdown_-1):count-executed;
            if(const auto n=state().block_step(cpu,memory,budget,limit,return_pc)){
                if(n>limit)throw std::runtime_error("static block exceeded execution slice");
                if(options.profile_execution)profile_countdown_-=n;
                executed_instructions+=n;executed+=n;++state().block_batches;state().block_instructions+=n;continue;
            }
        }
        if(state().sample_fallback && !--state().fallback_countdown){state().fallback_countdown=1021;++state().fallback_samples[{state().current_thread_id,cpu.eip}];}
        tick(budget,cpu.eip);
        if(options.profile_execution && !--profile_countdown_){profile_countdown_=4093;++profile_samples_[cpu.eip];}
        if(!step_(cpu,memory,budget))throw GuestFault(FaultKind::untranslated,cpu.eip,context);
        ++executed_instructions;++executed;
    }
    return executed;
}
U32 Process::callback(U32 address,std::span<const U32> args,bool stdcall) {
    if(args.size()>64)throw std::runtime_error("too many callback arguments");
    if(state().callback_depth>=64)throw GuestFault(FaultKind::budget,address,"guest callback nesting limit");
    const auto saved=cpu;++state().callback_depth;
    try {
        memory.check(saved.r[ESP]-U32((args.size()+1)*4),(args.size()+1)*4,Memory::Write);
        for(auto it=args.rbegin();it!=args.rend();++it)push(cpu,memory,*it);push(cpu,memory,CALLBACK_RETURN);cpu.eip=address;
        unsigned until_poll=function_execution()?1u:4095u;while(cpu.eip!=CALLBACK_RETURN && !exited_) {
            if(!until_poll){until_poll=function_execution()?1u:4096u;poll_services();}
            if(const auto n=execute_instructions(CALLBACK_RETURN,until_poll,"untranslated callback instruction")){until_poll-=n;continue;}
            --until_poll;
            if(!function_execution())tick(budget,cpu.eip);if(dispatch_api_boundary())continue;
            if(function_execution())throw GuestFault(FaultKind::untranslated,cpu.eip,"missing static callback function or host service");
            if(!step_(cpu,memory,budget))throw GuestFault(FaultKind::untranslated,cpu.eip,"untranslated callback instruction");++executed_instructions;
        }
        if(!exited_ && cpu.r[ESP]!=saved.r[ESP]-(stdcall?0u:U32(args.size()*4)))throw GuestFault(FaultKind::unsupported,cpu.eip,"callback ABI stack cleanup mismatch");
        if(!exited_)for(auto r:{EBX,ESI,EDI,EBP})if(cpu.r[r]!=saved.r[r])throw GuestFault(FaultKind::unsupported,cpu.eip,"callback did not preserve a nonvolatile register");
        const auto result=cpu.r[EAX];auto fp=cpu.fp;cpu=saved;cpu.fp=fp;--state().callback_depth;return result;
    }catch(...){--state().callback_depth;throw;}
}
U32 Process::run() {
    if(!loaded_)throw std::runtime_error("no loaded image");
    unsigned until_poll=std::max(1u,state().main_poll_interval)-1;while(!exited_ && cpu.eip!=PROCESS_RETURN) {
        if(!until_poll){poll_services();if(exited_)break;until_poll=std::max(1u,state().main_poll_interval);}
        if(const auto n=execute_instructions(PROCESS_RETURN,until_poll,"untranslated instruction target")){until_poll-=n;continue;}
        --until_poll;
        if(!function_execution())tick(budget,cpu.eip);if(dispatch_api_boundary())continue;
        if(function_execution())throw GuestFault(FaultKind::untranslated,cpu.eip,"missing static function or host service");
        if(!step_(cpu,memory,budget))throw GuestFault(FaultKind::untranslated,cpu.eip,"untranslated instruction target");++executed_instructions;
    }
    if(!exited_)exit(cpu.r[EAX]);return exit_code_;
}
void Process::set_error(U32 value){last_error=value;if(cpu.fs_valid)memory.store(cpu.fs_base+0x34,value,32);}
U32 Process::virtual_alloc(U32 at,U32 size,U32 allocation,U32 protect) {
    try {
        if(allocation&~0x3000u || !(allocation&0x3000u)){set_error(87);return 0;}
        const auto perm=protection(protect);const bool reserve=(allocation&0x2000u)!=0 || !at;
        const auto alignment=reserve?65536u:4096u;const auto base=at&~(alignment-1);
        const auto expanded=std::uint64_t(size)+(at-base);
        if(!size || expanded>512ull*1024*1024){set_error(87);return 0;}
        const auto length=page_size(U32(expanded));U32 chosen=base;
        if(!at) {
            chosen=0x10000000;
            if(!memory.available(chosen,length)){
                // Walk reservations once, jumping over entire occupied ranges.
                // Probing every 64 KiB repeatedly made loading large resources
                // increasingly expensive as the number of heap blocks grew.
                std::uint64_t candidate=chosen;
                for(const auto& region:memory.regions()){
                    const auto end=std::uint64_t(region.base)+region.size;
                    if(end<=candidate)continue;
                    if(std::uint64_t(region.base)>=candidate+length)break;
                    candidate=(end+65535u)&~std::uint64_t(65535u);
                }
                if(candidate+length>0x70000000ull){set_error(8);return 0;}
                chosen=U32(candidate);
            }
        }
        if(reserve){memory.reserve(chosen,length);try{if(allocation&0x1000u)memory.commit(chosen,length,perm);state().virtual_reservations.insert(chosen);}catch(...){memory.release(chosen);throw;}}
        else memory.commit(chosen,length,perm);
        return chosen;
    }catch(const std::exception&){set_error(487);return 0;}
}
bool Process::virtual_free(U32 at,U32 size,U32 operation) {
    try {
        if(operation==0x8000 && size==0){if(!state().virtual_reservations.contains(at)){set_error(487);return false;}memory.release(at);state().virtual_reservations.erase(at);return true;}
        if(operation==0x4000){auto region=memory.allocation(at);if(!state().virtual_reservations.contains(region.base)){set_error(487);return false;}if(!size){if(at!=region.base){set_error(87);return false;}memory.decommit(at,region.size);return true;}
            const auto base=at&~4095u;const auto expanded=std::uint64_t(size)+at-base;
            if(expanded>512ull*1024*1024){set_error(87);return false;}memory.decommit(base,page_size(U32(expanded)));return true;}
        set_error(87);return false;
    }catch(const std::exception&){set_error(487);return false;}
}
bool Process::virtual_protect(U32 at,U32 size,U32 protect,U32 old) {
    if(!size){set_error(87);return false;}
    memory.check(old,4,Memory::Write); // caller pointer faults are not allocation errors
    try {const auto base=at&~4095u;const auto expanded=std::uint64_t(size)+at-base;if(expanded>512ull*1024*1024){set_error(87);return false;}
        const auto prior=memory.protect(base,page_size(U32(expanded)),protection(protect));memory.store(old,native_protection(prior),32);return true;
    }catch(const std::exception&){set_error(487);return false;}
}
U32 Process::allocate_bytes(std::size_t size,unsigned permission) {
    if(size>512ull*1024*1024)throw std::runtime_error("oversized host-to-guest copy");
    auto p=virtual_alloc(0,U32(std::max<std::size_t>(size,1)),0x3000,native_protection(permission));if(!p)throw std::runtime_error("guest allocation failed");
    // Internal API buffers are not public VirtualAlloc reservations.
    state().virtual_reservations.erase(p);return p;
}
std::string Process::read_string(U32 p,std::size_t maximum) {
    auto scope=memory.execution_scope();std::string result;
    for(std::size_t n=0;n<maximum;){
        if(std::uint64_t(p)+n>0xffffffffull)throw GuestFault(FaultKind::memory,p,"string wraps");
        const auto bytes=scope.readable(p+U32(n),maximum-n);
        if(bytes.empty()){const auto c=memory.load(p+U32(n),8);if(!c)return result;result+=char(c);++n;continue;}
        const auto* end=static_cast<const std::uint8_t*>(std::memchr(bytes.data(),0,bytes.size()));
        const auto count=end?std::size_t(end-bytes.data()):bytes.size();result.append(reinterpret_cast<const char*>(bytes.data()),count);
        if(end)return result;n+=count;
    }
    throw GuestFault(FaultKind::memory,p,"unterminated bounded guest string");
}
U32 Process::string_length(U32 p,std::size_t maximum){
    auto scope=memory.execution_scope();
    for(std::size_t n=0;n<maximum;){
        if(std::uint64_t(p)+n>0xffffffffull)throw GuestFault(FaultKind::memory,p,"string wraps");
        const auto bytes=scope.readable(p+U32(n),maximum-n);
        if(bytes.empty()){if(!memory.load(p+U32(n),8))return U32(n);++n;continue;}
        const auto* end=static_cast<const std::uint8_t*>(std::memchr(bytes.data(),0,bytes.size()));
        if(end)return U32(n+std::size_t(end-bytes.data()));n+=bytes.size();
    }
    throw GuestFault(FaultKind::memory,p,"unterminated bounded guest string");
}
std::u16string Process::read_wstring(U32 p,std::size_t maximum) {
    std::u16string result;for(std::size_t n=0;n<maximum;++n){if(std::uint64_t(p)+n*2+2>0x100000000ull)throw GuestFault(FaultKind::memory,p,"wide string wraps");auto c=memory.load(p+U32(n*2),16);if(!c)return result;result+=char16_t(c);}throw GuestFault(FaultKind::memory,p,"unterminated bounded guest wide string");
}
U32 Process::put_string(const std::string& string){auto p=allocate_bytes(string.size()+1);memory.copy_in(p,std::span(reinterpret_cast<const std::uint8_t*>(string.c_str()),string.size()+1));return p;}
U32 Process::put_wstring(const std::u16string& string){auto p=allocate_bytes((string.size()+1)*2);for(std::size_t n=0;n<=string.size();++n)memory.store(p+U32(n*2),n==string.size()?0:U32(string[n]),16);return p;}
std::string Process::wall_profile() const {
    if(!options.profile_execution)return {};
    std::ostringstream out;out<<",\"wall_profile\":{\"guest_page_ms\":[";
    std::vector<std::pair<std::uint64_t,U32>> pages;for(const auto& [page,ns]:profile_wall_ns_)pages.emplace_back(ns,page);
    std::map<U32,std::vector<std::pair<std::uint64_t,U32>>> samples_by_page;
    for(const auto& [pc,count]:profile_samples_)samples_by_page[pc&~0xfffu].emplace_back(count,pc);
    std::sort(pages.rbegin(),pages.rend());bool first=true;
    for(std::size_t n=0;n<std::min<std::size_t>(pages.size(),48);++n){if(!first)out<<',';first=false;out<<"{\"page\":"<<pages[n].second<<",\"ms\":"<<pages[n].first/1000000.0<<",\"hot_pcs\":[";
        auto& samples=samples_by_page[pages[n].second];std::sort(samples.rbegin(),samples.rend());for(std::size_t k=0;k<std::min<std::size_t>(samples.size(),12);++k){if(k)out<<',';out<<"{\"pc\":"<<samples[k].second<<",\"samples\":"<<samples[k].first<<'}';}out<<"]}";}
    out<<"],\"api_inclusive_ms\":[";std::vector<std::pair<std::uint64_t,std::string>> apis;
    for(const auto& [name,ns]:profile_api_ns_)apis.emplace_back(ns,name);std::sort(apis.rbegin(),apis.rend());first=true;
    for(std::size_t n=0;n<std::min<std::size_t>(apis.size(),32);++n){if(!first)out<<',';first=false;out<<"{\"api\":"<<quote(apis[n].second)<<",\"ms\":"<<apis[n].first/1000000.0<<'}';}
    out<<"]}";return out.str();
}
std::string Process::api_call_profile() const {
    std::vector<const Api*> sorted;for(const auto& api:apis_)if(api->calls)sorted.push_back(api.get());
    std::sort(sorted.begin(),sorted.end(),[](const auto* a,const auto* b){return a->calls>b->calls;});
    std::ostringstream out;out<<",\"direct_leaf_api_calls\":"<<direct_leaf_api_calls<<",\"api_stack_snapshots\":"<<api_stack_snapshots<<",\"api_stack_slow_snapshots\":"<<api_stack_slow_snapshots<<",\"api_timing_sample_interval\":127,\"api_call_counts\":[";
    for(unsigned i=0;i<std::min<std::size_t>(64,sorted.size());++i){if(i)out<<',';const auto& api=*sorted[i];out<<"{\"api\":"<<quote(api.trace_name)<<",\"calls\":"<<api.calls<<",\"certified_leaf\":"<<(api.leaf?"true":"false")<<",\"timed_calls\":"<<api.timed_calls<<",\"sampled_ns\":"<<api.sampled_ns<<",\"max_sample_ns\":"<<api.max_sample_ns<<'}';}
    out<<']';return out.str();
}
std::string Process::report() const {
    std::ostringstream out;out<<"{\"schema\":\"winrecomp.process.v1\",\"input_sha256\":"<<quote(input_sha256)<<",\"exited\":"<<(exited_?"true":"false")<<",\"exit_code\":"<<exit_code_<<",\"eip\":"<<cpu.eip<<",\"esp\":"<<cpu.r[ESP]<<",\"native_instructions\":"<<executed_instructions<<",\"static_function_entries\":"<<native_function_entries<<",\"function_execution\":"<<(function_execution()?"true":"false")<<",\"api_calls\":"<<api_calls<<",\"budget_left\":"<<budget<<",\"recent_transfers\":[";
    bool first=true;for(const auto& x:recent_transfers){if(!first)out<<',';first=false;out<<"{\"pc\":"<<x.pc<<",\"return\":"<<x.return_address<<",\"api\":"<<quote(std::string(x.api))<<",\"arguments\":[";for(std::size_t n=0;n<x.argument_count;++n){if(n)out<<',';out<<x.arguments[n];}out<<"],\"detail\":"<<quote(x.detail)<<"}";}out<<"],\"gui\":"<<(gui_?gui_->report():"null")<<",\"directdraw\":"<<(directdraw_?directdraw_->report():"null");for(const auto& [name,report]:win32_->reports)out<<','<<quote(name)<<':'<<report();if(options.profile_execution){std::vector<std::pair<std::uint64_t,U32>> hot;for(const auto& [pc,n]:profile_samples_)hot.emplace_back(n,pc);std::sort(hot.rbegin(),hot.rend());std::uint64_t total=0;for(const auto& [n,pc]:hot)total+=n;out<<",\"execution_profile\":{\"sample_interval\":4093,\"samples\":"<<total<<",\"hot_pcs\":[";bool first_pc=true;for(std::size_t i=0;i<std::min<std::size_t>(hot.size(),128);++i){if(!first_pc)out<<',';first_pc=false;out<<"{\"pc\":"<<hot[i].second<<",\"samples\":"<<hot[i].first<<"}";}out<<"]}";}out<<api_call_profile()<<wall_profile();out<<",\"diagnostic_stop\":"<<(win32_->diagnostic_stop?"true":"false")<<"}";return out.str();
}
#ifndef WA2_STATIC_FUNCTIONS_ONLY
int run_program(int argc,char** argv,StepFunction step,const char* expected_sha256,void(*install_target)(Process&),std::span<const std::uint8_t> embedded_image) {
    std::unique_ptr<Process> process;std::string report_path,snapshot_path;std::uint64_t stop_after_presents=0;
    std::filesystem::path native_image,input_image;
    const bool external_image=argc>1 && std::string_view(argv[1]).substr(0,2)!="--";
    const bool standalone=!external_image && !embedded_image.empty();
    const auto same_file=[](const std::filesystem::path& first,const std::filesystem::path& second){
        if(first.empty() || second.empty())return false;
        return std::filesystem::weakly_canonical(first)==std::filesystem::weakly_canonical(second) || (std::filesystem::exists(first) && std::filesystem::exists(second) && std::filesystem::equivalent(first,second));
    };
    const auto protected_output=[&](const std::filesystem::path& value){return same_file(value,native_image) || same_file(value,input_image);};
    try {
#ifdef _WIN32
        std::vector<wchar_t> module_name(32768);const auto module_size=::GetModuleFileNameW(nullptr,module_name.data(),DWORD(module_name.size()));
        if(!module_size || module_size>=module_name.size())throw std::runtime_error("cannot resolve native executable location");
        native_image=std::filesystem::path(std::wstring(module_name.data(),module_size));
#else
        native_image=std::filesystem::absolute(argv[0]);
#endif
        if(external_image)input_image=argv[1];
        if(!external_image && !standalone)throw std::runtime_error("usage: recompiled_program original.exe [--root data-directory] [--budget N] [--report report.json] [--allow-write]");
        ProcessOptions options;
        options.image_name=external_image?input_image.filename().string():"WA2_chs.exe";options.command_line='"'+options.image_name+'"';
        if(standalone){
            options.instruction_budget=std::numeric_limits<std::uint64_t>::max();options.allow_file_write=true;
            const auto directory=native_image.parent_path();bool found=false;
            for(const auto& candidate:{directory,directory/"res",directory.parent_path()/"res"}){
                if(std::filesystem::is_regular_file(candidate/"grp.pak") && std::filesystem::is_regular_file(candidate/"fnt.pak")){options.data_root=candidate;found=true;break;}
            }
            if(!found)options.data_root=directory;
        }
        for(int n=external_image?2:1;n<argc;++n){const std::string arg=argv[n];if(arg=="--allow-write"){options.allow_file_write=true;continue;}if(arg=="--read-only"){options.allow_file_write=false;continue;}if(arg=="--profile"){options.profile_execution=true;continue;}if(arg=="--no-pause")continue;
            if(n+1>=argc)throw std::runtime_error("missing option value");const std::string value=argv[++n];
            if(arg=="--root")options.data_root=value;else if(arg=="--report")report_path=value;else if(arg=="--snapshot")snapshot_path=value;else if(arg=="--command-line")options.command_line=value;
            else if(arg=="--budget"){std::size_t used{};options.instruction_budget=std::stoull(value,&used);if(used!=value.size() || value.empty() || value[0]=='-')throw std::runtime_error("invalid budget");}
            else if(arg=="--frames"){std::size_t used{};stop_after_presents=std::stoull(value,&used);if(used!=value.size() || value.empty() || value[0]=='-' || !stop_after_presents)throw std::runtime_error("invalid frame limit");}
            else throw std::runtime_error("unknown runtime option: "+arg);
        }
        if(standalone && report_path.empty())report_path=(options.data_root/"wa2-runtime.json").string();
        if(standalone && (!std::filesystem::is_regular_file(options.data_root/"grp.pak") || !std::filesystem::is_regular_file(options.data_root/"fnt.pak")))throw std::runtime_error("Game resources not found. Place this EXE beside grp.pak and fnt.pak, or supply --root <resource-directory>.");
        if(!report_path.empty()) {
            if(protected_output(report_path)){report_path.clear();throw std::runtime_error("report must not overwrite an executable");}
        }
        if(!snapshot_path.empty()){
            const auto output=std::filesystem::weakly_canonical(snapshot_path);
            if(protected_output(output))throw std::runtime_error("snapshot must not overwrite an executable");
            if(!report_path.empty() && (output==std::filesystem::weakly_canonical(report_path) || (std::filesystem::exists(output) && std::filesystem::exists(report_path) && std::filesystem::equivalent(output,report_path))))throw std::runtime_error("snapshot and runtime report must use different files");
        }
        const auto image=external_image?Image::load(input_image):Image::parse(std::vector<std::uint8_t>(embedded_image.begin(),embedded_image.end()));if(sha256(image.bytes)!=expected_sha256)throw std::runtime_error("input SHA-256 does not match statically translated image");
        process=std::make_unique<Process>(step,options);if(install_target)install_target(*process);process->load(image);process->state().snapshot_path=snapshot_path;process->state().stop_after_presents=stop_after_presents;
        if(!report_path.empty())write_file(report_path,process->report()+"\n");
        if(!report_path.empty())process->state().poll.push_back([target=process.get(),report_path,last=std::chrono::steady_clock::now()]() mutable {const auto now=std::chrono::steady_clock::now();if(now-last>=std::chrono::seconds(1)){write_file(report_path,target->report()+"\n");last=now;}});
        const auto result=process->run();
        const auto report=process->report();if(!report_path.empty())write_file(report_path,report+"\n");std::cout<<report<<'\n';return int(result&255u);
    }catch(const std::exception& e) {
        // Even failures during option parsing must never turn the diagnostic
        // path into permission to overwrite the input (including hard links).
        try {if(!report_path.empty() && protected_output(report_path))report_path.clear();}catch(...){report_path.clear();}
        const auto report=std::string("{\"error\":")+quote(e.what())+",\"process\":"+(process?process->report():"null")+"}\n";
        try{if(!report_path.empty())write_file(report_path,report);}catch(const std::exception& report_error){std::cerr<<report_error.what()<<'\n';}
        std::cerr<<report;
#ifdef _WIN32
        if(standalone && argc==1)::MessageBoxA(nullptr,e.what(),"WA2 x64 startup failed",MB_OK|MB_ICONERROR);
#endif
        return 2;
    }
}
#endif
}
