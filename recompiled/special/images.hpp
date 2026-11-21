#pragma once
#include <winrecomp/runtime.hpp>
#include <string>
namespace wa2::special {
bool image_chroma(wr::Cpu&,wr::Memory&);
bool image_colour(wr::Cpu&,wr::Memory&);
std::string image_pixel_report();
}
