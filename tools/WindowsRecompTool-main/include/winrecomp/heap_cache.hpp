#pragma once
#include <cstdint>
namespace wr {
class Process;
// Sidecar state deliberately preserves Win32State and Heap's existing ABI.
struct LargeHeapCacheStatistics {
    unsigned idle_large_blocks{};
    std::uint64_t idle_large_bytes{},peak_idle_large_bytes{},large_block_reuses{},large_block_retained{},large_block_evictions{};
};
LargeHeapCacheStatistics large_heap_cache_statistics(Process&);
void set_large_heap_cache_enabled(Process&,bool);
}
