#pragma once
#include "byte_equality.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <span>

namespace wa2::text {
// Data-only index of the original ordered range table. No game address, View,
// file, pixel or OS object is retained. Recheck all 256 source bytes on lookup.
class GlyphIndex {
    std::array<std::uint8_t,256> source_{};
    std::unique_ptr<std::uint32_t[]> indices_;
    std::uint32_t total_{},rebuilds_{};
    bool loaded_{},terminated_{};
    static constexpr std::uint32_t Missing=0xffffffffu;
    static unsigned u16(const std::uint8_t* p){return unsigned(p[0])|(unsigned(p[1])<<8);}
public:
    static constexpr unsigned Entries=65536,MaximumHeapBytes=Entries*sizeof(std::uint32_t);
    std::uint32_t rebuilds()const{return rebuilds_;}
    std::optional<std::uint32_t> lookup(std::span<const std::uint8_t,256> source,std::uint16_t code){
        if(!loaded_ || !wa2::memory::equal_bytes<256>(source_.data(),source.data())){
            if(!indices_){indices_.reset(new(std::nothrow) std::uint32_t[Entries]);if(!indices_)return {};}
            std::fill_n(indices_.get(),Entries,Missing);total_=0;terminated_=false;
            for(unsigned n=0;n<64;++n){const unsigned first=u16(source.data()+n*4),last=u16(source.data()+n*4+2);
                if(last==0xffff || (n && first==0xffff)){terminated_=true;break;}
                if(first>last)break;
                // Overlaps and unsorted tables keep the first source match;
                // invalid trailing rows cannot hide an earlier valid match.
                for(unsigned code=first;code<=last;++code)if(indices_[code]==Missing)indices_[code]=total_+code-first;
                total_+=last-first+1;
            }
            std::memcpy(source_.data(),source.data(),source.size());loaded_=true;++rebuilds_;
        }
        if(indices_[code]!=Missing)return indices_[code];
        return terminated_?std::optional<std::uint32_t>{total_}:std::nullopt;
    }
};
}
