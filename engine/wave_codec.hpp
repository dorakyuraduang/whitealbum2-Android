#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
namespace wa2::audio {
// Native, bounded WAVEFORMATEX block conversion. Platform API adapters only
// serialize source fields and stream headers; they never decode CPU opcodes.
struct WaveCodecFormat {
    std::uint16_t tag{},channels{},block{},bits{};
    std::uint32_t rate{},bytes_per_second{};
    std::vector<std::uint8_t> extra;
    static WaveCodecFormat parse(std::span<const std::uint8_t>);
    bool supported()const;
    unsigned block_frames()const;
};
class WaveBlockDecoder {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    explicit WaveBlockDecoder(WaveCodecFormat);
    ~WaveBlockDecoder();
    WaveBlockDecoder(const WaveBlockDecoder&)=delete;
    std::vector<std::uint8_t> decode(std::span<const std::uint8_t> block);
    void reset();
};
}
