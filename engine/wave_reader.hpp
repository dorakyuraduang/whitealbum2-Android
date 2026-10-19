#pragma once
#include "resource_archive.hpp"

namespace wa2::audio{
struct WaveFormat{unsigned channels{},rate{};bool operator==(const WaveFormat&)const=default;};
// Native RIFF PCM reader. Output is signed 16-bit; PCM16 stays byte-exact and
// unsigned PCM8 expands exactly. Compressed WAV codecs fail explicitly.
class WaveReader{
    std::shared_ptr<resource::Source> source_;
    WaveFormat format_;
    std::uint64_t data_{},bytes_{},cursor_{};
    unsigned sample_bytes_{};
public:
    explicit WaveReader(std::shared_ptr<resource::Source>);
    WaveFormat format()const noexcept{return format_;}
    std::uint64_t frames()const noexcept{return bytes_/format_.channels/sample_bytes_;}
    std::size_t read(std::span<std::uint8_t> pcm16);
    void rewind()noexcept{cursor_=0;}
    bool finished()const noexcept{return cursor_==bytes_;}
};
}
