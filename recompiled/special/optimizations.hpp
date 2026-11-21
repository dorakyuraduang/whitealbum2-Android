#pragma once
#include <winrecomp/runtime.hpp>
#include <string>
namespace wa2::special {
inline thread_local bool optimization_reference{};
struct OptimizationReferenceScope {
    bool saved{optimization_reference};
    OptimizationReferenceScope(){optimization_reference=true;}
    ~OptimizationReferenceScope(){optimization_reference=saved;}
};
bool scalar_optimization(wr::Cpu&,wr::Memory&,wr::Memory::ExecutionScope&);
std::string scalar_optimization_report();
std::uint64_t scalar_optimization_activity();
}
