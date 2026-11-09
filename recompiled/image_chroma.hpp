#pragma once
#include <winrecomp/runtime.hpp>
namespace wa2::aot {
bool native_image_chroma(wr::Cpu&,wr::Memory&);
struct ImageChromaCounts {std::uint64_t attempts{},completed{},pixels{},grey_pixels{};};
ImageChromaCounts image_chroma_counts();
}
