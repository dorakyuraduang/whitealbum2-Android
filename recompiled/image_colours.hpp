#pragma once
#include <winrecomp/runtime.hpp>

namespace wa2::aot {
// These six closed loops belong to the cached 24/32-bit image loaders. They
// have no service, scheduling, floating-point or allocation boundary. Enter
// at each preheader, so a declined loop never retries the hook per pixel.
bool native_image_colours(wr::Cpu&,wr::Memory&);
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
