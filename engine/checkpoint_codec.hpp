#pragma once
#include <bit>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace wa2::checkpoint {
using Bytes=std::vector<std::uint8_t>;
inline constexpr std::size_t maximum_bytes=64u*1024*1024;
inline std::uint32_t crc(std::span<const std::uint8_t> bytes){
    static constexpr auto table=[]{std::array<std::uint32_t,256> values{};for(unsigned n=0;n<values.size();++n){auto value=std::uint32_t(n);for(unsigned bit=0;bit<8;++bit)value=(value>>1)^(0xedb88320u&std::uint32_t(0-int(value&1)));values[n]=value;}return values;}();
    std::uint32_t value=0xffffffffu;
    for(auto byte:bytes)value=(value>>8)^table[(value^byte)&255];
    return ~value;
}
class Writer {
    Bytes data_;
public:
    void raw(std::span<const std::uint8_t> bytes){
        if(bytes.size()>maximum_bytes-data_.size())throw std::runtime_error("Native checkpoint exceeds size limit");
        data_.insert(data_.end(),bytes.begin(),bytes.end());
    }
    void u32(std::uint32_t value){std::uint8_t b[4];for(unsigned i=0;i<4;++i)b[i]=std::uint8_t(value>>(i*8));raw(b);}
    void i32(std::int32_t value){u32(std::bit_cast<std::uint32_t>(value));}
    void u64(std::uint64_t value){u32(std::uint32_t(value));u32(std::uint32_t(value>>32));}
    void boolean(bool value){u32(value?1:0);}
    void blob(std::span<const std::uint8_t> bytes){if(bytes.size()>maximum_bytes)throw std::runtime_error("Native checkpoint blob exceeds limit");u32(std::uint32_t(bytes.size()));raw(bytes);}
    void string(std::string_view value){blob({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});}
    Bytes seal(std::string_view tag)const{
        if(tag.size()!=4 || data_.size()>maximum_bytes-16)throw std::runtime_error("Invalid native checkpoint envelope");
        Writer output;output.raw({reinterpret_cast<const std::uint8_t*>(tag.data()),4});output.u32(1);output.blob(data_);output.u32(crc(output.data_));return std::move(output.data_);
    }
};
class Reader {
    std::span<const std::uint8_t> bytes_;
    std::size_t at_{};
public:
    explicit Reader(std::span<const std::uint8_t> bytes):bytes_(bytes){}
    static Reader open(std::string_view tag,std::span<const std::uint8_t> bytes){
        if(tag.size()!=4 || bytes.size()<16 || bytes.size()>maximum_bytes ||
           std::string_view(reinterpret_cast<const char*>(bytes.data()),4)!=tag)throw std::runtime_error("Invalid native checkpoint header");
        Reader head(bytes.subspan(4));if(head.u32()!=1)throw std::runtime_error("Unsupported native checkpoint version");
        const auto data=head.blob();const auto checksum=head.u32();head.finish();
        if(crc(bytes.first(bytes.size()-4))!=checksum)throw std::runtime_error("Native checkpoint checksum mismatch");
        return Reader(data);
    }
    std::span<const std::uint8_t> raw(std::size_t count){
        if(count>bytes_.size()-at_)throw std::runtime_error("Truncated native checkpoint");
        auto value=bytes_.subspan(at_,count);at_+=count;return value;
    }
    std::uint32_t u32(){const auto b=raw(4);return std::uint32_t(b[0])|(std::uint32_t(b[1])<<8)|(std::uint32_t(b[2])<<16)|(std::uint32_t(b[3])<<24);}
    std::int32_t i32(){return std::bit_cast<std::int32_t>(u32());}
    std::uint64_t u64(){const auto lo=u32();return lo|(std::uint64_t(u32())<<32);}
    bool boolean(){const auto value=u32();if(value>1)throw std::runtime_error("Invalid native checkpoint boolean");return value!=0;}
    std::span<const std::uint8_t> blob(std::size_t maximum=maximum_bytes){const auto count=u32();if(count>maximum)throw std::runtime_error("Native checkpoint field exceeds limit");return raw(count);}
    std::string string(std::size_t maximum=1024*1024){const auto b=blob(maximum);return {reinterpret_cast<const char*>(b.data()),b.size()};}
    bool empty()const noexcept{return at_==bytes_.size();}
    void finish()const{if(at_!=bytes_.size())throw std::runtime_error("Trailing native checkpoint data");}
};
}
