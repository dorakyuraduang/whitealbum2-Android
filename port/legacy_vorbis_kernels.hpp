#pragma once
#include <winrecomp/runtime.hpp>
namespace wr {
// Only comparison executables provide the old codec's instruction kernels.
unsigned wa2_legacy_vorbis_batch(Cpu&,Memory&,std::uint64_t&,unsigned,U32);
}
