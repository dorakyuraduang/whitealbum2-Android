#pragma once
#include <array>
#include <cstdint>
#include <bit>
#include <cstring>
namespace wr {
// FXSAVE64 layout. x87 values remain ten-byte extended-precision bit patterns;
// Arithmetic backends preserve this serialization across host architectures.
struct alignas(16) FpState {
    std::array<std::uint8_t,512> bytes{};
    FpState() { put16(0,0x037f);put32(24,0x1f80); }
    template<class Value> [[gnu::always_inline]] Value read(unsigned off) const {
        if constexpr(std::endian::native==std::endian::little)if(off<=bytes.size()-sizeof(Value)){
            Value result;std::memcpy(&result,bytes.data()+off,sizeof(Value));return result;
        }
        Value result{};for(unsigned n=0;n<sizeof(Value);++n)result|=Value(bytes.at(off+n))<<(n*8);return result;
    }
    template<class Value> [[gnu::always_inline]] void write(unsigned off,Value value){
        if constexpr(std::endian::native==std::endian::little)if(off<=bytes.size()-sizeof(Value)){
            std::memcpy(bytes.data()+off,&value,sizeof(Value));return;
        }
        // Keep the existing byte order and partial-write behavior on an invalid
        // offset. Normal internal offsets compile to one unaligned-safe move.
        for(unsigned n=0;n<sizeof(Value);++n)bytes.at(off+n)=std::uint8_t(value>>(n*8));
    }
    [[gnu::always_inline]] std::uint16_t get16(unsigned off) const {return read<std::uint16_t>(off);}
    [[gnu::always_inline]] std::uint32_t get32(unsigned off) const {return read<std::uint32_t>(off);}
    [[gnu::always_inline]] std::uint64_t get64(unsigned off) const {return read<std::uint64_t>(off);}
    [[gnu::always_inline]] void put16(unsigned off,std::uint16_t value){write(off,value);}
    [[gnu::always_inline]] void put32(unsigned off,std::uint32_t value){write(off,value);}
    [[gnu::always_inline]] void put64(unsigned off,std::uint64_t value){write(off,value);}
    void clear_x87_padding(unsigned index){
        constexpr std::uint32_t zero32{};constexpr std::uint16_t zero16{};
        std::memcpy(bytes.data()+42+index*16,&zero32,4);std::memcpy(bytes.data()+46+index*16,&zero16,2);
    }
    void clear_x87_padding(){for(unsigned index=0;index<8;++index)clear_x87_padding(index);}
    unsigned top() const {return (get16(2)>>11)&7u;}
    std::uint16_t full_tag() const {
        unsigned result=0;
        for(unsigned physical=0;physical<8;++physical) {
            unsigned tag=3;
            if(bytes[4]&(1u<<physical)) {
                const auto logical=(physical+8-top())&7u;const auto at=32+logical*16;
                const auto sig=get64(at);const auto exponent=get16(at+8)&0x7fffu;
                tag=(!exponent && !sig)?1u:(exponent && exponent!=0x7fffu && (sig>>63))?0u:2u;
            }
            result|=tag<<(physical*2);
        }
        return std::uint16_t(result);
    }
};
static_assert(alignof(FpState)>=16);
}
