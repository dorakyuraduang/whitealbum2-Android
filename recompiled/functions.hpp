#pragma once
#include <winrecomp/compiled.hpp>
#include "floating.hpp"
#include "integer_value.hpp"
#include "host_calls.hpp"
#include <span>
#ifndef WA2_AOT_DIAGNOSTICS
#define WA2_AOT_DIAGNOSTICS 0
#endif

namespace wa2::aot {
// This is the original program's ABI state, not an instruction interpreter.
// Both host architectures compile the same generated function bodies.
using Context = wr::Cpu;
class Program;
using Scope = wr::Memory::ExecutionScope;
struct CodeSpan {std::uint32_t address;std::span<const std::uint8_t> bytes;};
using Body = void(*)(Context&,wr::Memory&,Scope&,std::uint64_t&,bool,std::uint32_t);
struct Function {std::uint32_t entry;Body body;std::span<const CodeSpan> code;std::span<const std::uint32_t> callees;};
struct Location {const Function* function{};std::uint32_t resume{};};
std::span<const Function> functions() noexcept;
Location locate(std::uint32_t entry) noexcept;
const Function* find(std::uint32_t entry) noexcept;
#if WA2_AOT_DIAGNOSTICS
inline thread_local bool native_integer_registers_enabled=true;
struct NativeIntegerReferenceScope {
    bool previous{native_integer_registers_enabled};
    NativeIntegerReferenceScope(){native_integer_registers_enabled=false;}
    ~NativeIntegerReferenceScope(){native_integer_registers_enabled=previous;}
};
inline thread_local bool native_indirect_calls_enabled=true;
struct NativeIndirectReferenceScope {
    bool previous{native_indirect_calls_enabled};
    NativeIndirectReferenceScope(){native_indirect_calls_enabled=false;}
    ~NativeIndirectReferenceScope(){native_indirect_calls_enabled=previous;}
};
#endif

// Proofs belong to a particular Memory lifetime, and are invalidated whenever
// executable bytes/protection change. They are never keyed by a recycled ptr.
class Program {
    wr::Memory& memory_;
    std::uint64_t epoch_{~std::uint64_t(0)};
    // 0 = unseen, 1 = visiting, 2 = proven. Recursive call graphs are legal.
    std::vector<std::uint8_t> verified_;
    void ensure(const Function& function);
    void prove(const Function& function);
    bool invoke_indirect(Context&,Scope&);
public:
    explicit Program(wr::Memory& memory):memory_(memory),verified_(functions().size()){}
    // Unknown entries return false before changing state; there is no decoder,
    // silent skip, or interpreter fallback inside this shared implementation.
    bool invoke(Context&,std::uint64_t& budget);
    bool invoke_unmetered(Context&);
};
}
