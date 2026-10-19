#include "fp_kernel_cases.hpp"
extern "C" __attribute__((visibility("default"))) unsigned wa2_portable_fp_forms(){return unsigned(fp_kernel_cases().size());}
extern "C" __attribute__((visibility("default"))) unsigned wa2_portable_simd_forms(){return unsigned(simd_kernel_cases().size());}
