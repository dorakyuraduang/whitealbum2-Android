#pragma once
#include <winrecomp/runtime.hpp>
namespace wa2::aot {
// A shared allowance covers indirect compiled calls and direct host services.
// It bounds work between scheduler boundaries and native recursion depth.
struct NativeCalls {
    bool (*invoke)(wr::Cpu&,wr::Memory&,wr::Memory::ExecutionScope&,void*){};
    void* owner{};unsigned remaining{32};
};
inline thread_local NativeCalls* current_native_calls{};
class NativeCallScope {
    NativeCalls calls_;NativeCalls* previous_{current_native_calls};
public:
    NativeCallScope(bool (*invoke)(wr::Cpu&,wr::Memory&,wr::Memory::ExecutionScope&,void*),void* owner):calls_{invoke,owner}{current_native_calls=&calls_;}
    ~NativeCallScope(){current_native_calls=previous_;}
    NativeCallScope(const NativeCallScope&)=delete;
};
// The runtime opts in only leaf services with no callback, wait, scheduler
// switch, or CPU transfer. All other calls unwind to the outer host boundary.
// A finite allowance retains regular scheduling opportunities even for a
// source loop that consists entirely of these small calls.
struct LeafServices {
    bool (*invoke)(wr::Cpu&,wr::Memory&,void*){};
    void* owner{};
    unsigned remaining{32};
};
inline thread_local LeafServices* current_leaf_services{};
class LeafServiceScope {
    LeafServices services_;
    LeafServices* previous_{current_leaf_services};
public:
    LeafServiceScope(bool (*invoke)(wr::Cpu&,wr::Memory&,void*),void* owner):services_{invoke,owner}{current_leaf_services=&services_;}
    ~LeafServiceScope(){current_leaf_services=previous_;}
    LeafServiceScope(const LeafServiceScope&)=delete;
};
inline bool call_leaf_service(wr::Cpu& cpu,wr::Memory& memory){
    auto* service=current_leaf_services;
    if(!service)return false;
    auto& remaining=current_native_calls?current_native_calls->remaining:service->remaining;
    if(!remaining)return false;--remaining;return service->invoke(cpu,memory,service->owner);
}
inline bool call_source_indirect(wr::Cpu& cpu,wr::Memory& memory,wr::Memory::ExecutionScope& scope){
    if(call_leaf_service(cpu,memory))return true;
    auto* calls=current_native_calls;if(!calls || !calls->invoke || !calls->remaining)return false;
    --calls->remaining;return calls->invoke(cpu,memory,scope,calls->owner);
}
}
