#include "font_glyphs.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace wa2::text {
namespace {
struct CodeRange {std::uint16_t first,last;};
#include "font_ranges.inc"
struct Range {std::uint16_t first,last;std::uint32_t index;};
template<std::size_t N>struct Lookup {
    std::array<Range,N> ranges{};
    std::uint32_t count{};
    constexpr explicit Lookup(const std::array<CodeRange,N>& source){
        for(std::size_t i=0;i<N;++i){ranges[i]={source[i].first,source[i].last,count};count+=std::uint32_t(source[i].last)-source[i].first+1;}
        // Atlas ordering is deliberately different from numeric code ordering.
        // Keep the original glyph bases while sorting only the search index.
        std::sort(ranges.begin(),ranges.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    }
    std::uint32_t find(std::uint16_t code) const noexcept {
        const auto end=std::upper_bound(ranges.begin(),ranges.end(),code,[](auto value,const Range& range){return value<range.first;});
        if(end==ranges.begin())return count;
        const auto& range=*(end-1);return code<=range.last?range.index+code-range.first:count;
    }
};
static constexpr Lookup base(base_ranges);
static constexpr Lookup extended(extended_ranges);
}
std::uint32_t glyph_index(std::uint16_t code,bool extended_set) noexcept {return extended_set?extended.find(code):base.find(code);}
std::uint32_t glyph_count(bool extended_set) noexcept {return extended_set?extended.count:base.count;}
GlyphBox glyph_box(std::uint16_t code,bool extended_set,std::uint32_t cell,std::uint32_t inset,std::uint32_t extent){
    if(!cell || cell>4096 || inset>cell || extent>cell-inset)throw std::runtime_error("invalid native glyph cell");
    const auto index=glyph_index(code,extended_set);
    return {(index%80)*cell+inset,(index/80)*cell+inset,extent,extent};
}
}
