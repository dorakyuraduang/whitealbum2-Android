#pragma once
#include <cstddef>
#include <cstdint>
namespace wr {
struct Wa2NativeCodeLayout {std::uintptr_t begin,end,points;std::size_t count;};
Wa2NativeCodeLayout wa2_linear_code_layout();
bool wa2_linear_available();
unsigned wa2_linear_call(void* state,const void* base,unsigned limit);
std::uint64_t wa2_linear_fault_count();
}
