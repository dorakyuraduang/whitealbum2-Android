#pragma once
#include <winrecomp/runtime.hpp>
namespace wr {
// One complete, checked iteration of the statically translated MDCT butterfly.
// Zero means the original instruction bodies must execute instead.
unsigned wa2_vorbis_batch(Cpu&,Memory&,std::uint64_t& budget,unsigned limit,U32 return_pc);
}
