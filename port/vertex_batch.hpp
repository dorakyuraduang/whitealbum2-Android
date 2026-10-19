#pragma once
#include <winrecomp/runtime.hpp>
namespace wr {
bool wa2_vertex_full(Cpu&,Memory&);
unsigned wa2_vertex_batch(Cpu&,Memory&,std::uint64_t&,unsigned,U32);
}
