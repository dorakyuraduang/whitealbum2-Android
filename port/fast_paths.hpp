#pragma once
#include <winrecomp/runtime.hpp>
#include "sprite_sort.hpp"
namespace wr {
class Process;
unsigned wa2_static_batch(Cpu&,Memory&,std::uint64_t&,unsigned,U32);
void install_wa2_fast_paths(Process&);
}
