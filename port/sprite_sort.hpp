#pragma once
#include <winrecomp/runtime.hpp>
#include <string>
namespace wr {
bool wa2_sort_objects_full(Cpu&,Memory&);
unsigned wa2_sort_objects(Cpu&,Memory&,std::uint64_t&,unsigned,U32);
unsigned wa2_sort_shift(Cpu&,Memory&,std::uint64_t&,unsigned,U32);
std::string wa2_sort_statistics();
// Development comparisons only; release execution keeps partial search on.
void wa2_partial_sort_search(bool);
}
