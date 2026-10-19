#pragma once
#include <winrecomp/runtime.hpp>
namespace wr {
// Exact whole-function batch for the original bounded Vorbis bit peek.
// Zero leaves all state untouched and requests ordinary translated execution.
unsigned wa2_bitreader_batch(Cpu&,Memory&,std::uint64_t& budget,unsigned limit,U32 return_pc);
}
