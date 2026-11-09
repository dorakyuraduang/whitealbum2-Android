#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace wa2::image {
struct ChromaTables {std::array<std::uint8_t,256> channel,grey;};
// Exact integer/table transform of the original 24/32-bit loaders. A null
// table selects grayscale; alpha and inter-pixel padding remain untouched.
void convert_chroma(std::span<std::uint8_t>,unsigned pixels,unsigned stride,bool first_red,const ChromaTables*);
}
