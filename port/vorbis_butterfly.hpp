#pragma once
#include <winrecomp/runtime.hpp>
namespace wr {
// Checked 404-instruction 32-point MDCT butterfly at 0x46e477.
unsigned wa2_vorbis_butterfly(Cpu&,Memory&,std::uint64_t& budget,unsigned limit,U32 return_pc);
}
