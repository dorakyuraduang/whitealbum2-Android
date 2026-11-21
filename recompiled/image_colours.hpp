#pragma once
#include <winrecomp/runtime.hpp>
#include <array>

namespace wa2::aot {
// These six closed loops belong to the cached 24/32-bit image loaders. They
// have no service, scheduling, floating-point or allocation boundary. Enter
// at each preheader, so a declined loop never retries the hook per pixel.
struct ImageColourSource {
    std::array<wr::U32,6> entries,ends;
    bool frame_pointer{};
};
inline constexpr ImageColourSource main_image_colour_source{
    {0x439156,0x4391de,0x43922a,0x4392e6,0x43936e,0x4393ba},
    {0x439193,0x439208,0x439257,0x439323,0x439398,0x4393e7}};
bool native_image_colours(wr::Cpu&,wr::Memory&,const ImageColourSource& = main_image_colour_source);
struct ImageColourCounts {std::uint64_t attempts{},completed{},pixels{};};
ImageColourCounts image_colour_counts();
#if defined(WA2_AOT_DIAGNOSTICS) && WA2_AOT_DIAGNOSTICS
inline thread_local bool image_colours_enabled=true;
struct ImageColourReferenceScope {
    bool previous{image_colours_enabled};
    ImageColourReferenceScope(){image_colours_enabled=false;}
    ~ImageColourReferenceScope(){image_colours_enabled=previous;}
};
#endif
}
