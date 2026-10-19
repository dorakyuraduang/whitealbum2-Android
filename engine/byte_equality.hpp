#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace wa2::memory {
// Equality only: the integer reduction vectorizes on both host architectures.
// Every load is within the exact byte range, including unaligned buffers. Do
// not replace this with a lexical memcmp call: size-oriented C runtimes may
// compare large equal blocks byte by byte.
template<std::size_t Bytes> bool equal_bytes(const std::uint8_t* first,const std::uint8_t* second)noexcept{
    std::uint64_t different{};
    for(std::size_t at=0;at<Bytes/8*8;at+=8){std::uint64_t a,b;std::memcpy(&a,first+at,8);std::memcpy(&b,second+at,8);different|=a^b;}
    for(std::size_t at=Bytes/8*8;at<Bytes;++at)different|=first[at]^second[at];
    return different==0;
}
inline bool equal_bytes(std::span<const std::uint8_t> first,std::span<const std::uint8_t> second)noexcept{
    if(first.size()!=second.size())return false;
    std::uint64_t different{};const auto whole=first.size()/8*8;
    for(std::size_t at=0;at<whole;at+=8){std::uint64_t a,b;std::memcpy(&a,first.data()+at,8);std::memcpy(&b,second.data()+at,8);different|=a^b;}
    for(std::size_t at=whole;at<first.size();++at)different|=first[at]^second[at];return different==0;
}
inline bool equal_words(std::span<const std::uint32_t> first,std::span<const std::uint32_t> second)noexcept{
    if(first.size()!=second.size())return false;
    std::uint32_t different{};for(std::size_t n=0;n<first.size();++n)different|=first[n]^second[n];return different==0;
}
}
