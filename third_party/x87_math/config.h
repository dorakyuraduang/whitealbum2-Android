#pragma once
#include <cstdint>
using Bit8u=std::uint8_t;using Bit8s=std::int8_t;
using Bit16u=std::uint16_t;using Bit16s=std::int16_t;
using Bit32u=std::uint32_t;using Bit32s=std::int32_t;
using Bit64u=std::uint64_t;using Bit64s=std::int64_t;
#define BX_LITTLE_ENDIAN 1
#define BX_CPP_INLINE inline
#define BX_CONST64(value) UINT64_C(value)
#define BX_HAVE___BUILTIN_CLZ 1
#define BX_HAVE_INT128 1
#define SOFTFLOAT_FAST_DIV64TO32 1
