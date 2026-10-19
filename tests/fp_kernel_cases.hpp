#pragma once
#include <cstdint>
#include <span>
using FpKernel=void(*)(void*,void*,std::uint32_t*);
struct FpKernelCase {const char* name;unsigned opcode,encoding;bool memory;FpKernel portable,native,registers;};
std::span<const FpKernelCase> fp_kernel_cases();
struct SimdKernelCase {const char* name;unsigned prefix,opcode,encoding;bool memory;unsigned immediate;FpKernel portable,native;};
std::span<const SimdKernelCase> simd_kernel_cases();
