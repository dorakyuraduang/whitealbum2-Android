#pragma once
#include <winrecomp/runtime.hpp>
#include <array>
namespace wa2::aot {
struct ImageChromaSource {
    std::array<wr::U32,6> entries,ends;
    wr::U32 channel;
    bool frame_pointer{};
};
inline constexpr ImageChromaSource main_image_chroma_source{
    {0x43884d,0x438898,0x438906,0x438984,0x438a23,0x438a85},
    {0x438889,0x4388d5,0x438971,0x4389f1,0x438a65,0x438af1},0xa83db0};
bool native_image_chroma(wr::Cpu&,wr::Memory&,const ImageChromaSource& = main_image_chroma_source);
struct ImageChromaCounts {std::uint64_t attempts{},completed{},pixels{},grey_pixels{};};
ImageChromaCounts image_chroma_counts();
}
