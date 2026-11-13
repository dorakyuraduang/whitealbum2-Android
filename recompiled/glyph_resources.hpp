#pragma once
#include <cstdint>
namespace wa2::aot {
struct GlyphResourceCounts {
    std::uint64_t single_texture_handle_reads{},full_texture_tables{},full_texture_handle_words{};
};
inline thread_local GlyphResourceCounts glyph_resource_counts;
}
