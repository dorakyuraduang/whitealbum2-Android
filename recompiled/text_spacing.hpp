#pragma once
#include <winrecomp/runtime.hpp>
namespace wa2::aot {
// Source text's decimal spacing carry, with the same registers/flags/stores.
// Requires the matching ExecutionScope already held by the static caller.
bool native_text_spacing(wr::Cpu&,wr::Memory&,wr::Memory::ExecutionScope&);
}
