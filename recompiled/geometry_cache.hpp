#pragma once
#include <cstdint>
namespace wa2::aot {
struct GeometryCacheCounts {std::uint64_t grid_hits{},grid_misses{},sprite_hits{},sprite_misses{},grid_captures{},sprite_captures{};};
GeometryCacheCounts geometry_cache_counts();
#if WA2_AOT_DIAGNOSTICS
extern thread_local bool native_geometry_cache_enabled;
#endif
}
