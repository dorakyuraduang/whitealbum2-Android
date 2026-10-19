#pragma once
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace wa2::audio {
struct StreamStatus {
    std::size_t queued_frames{},free_frames{};
    std::uint64_t consumed_frames{},underruns{};
    bool playing{},finished{},input_finished{};
};
// A bounded producer/consumer queue of signed 16-bit PCM, independent of any
// operating system or decoder. Empty output is silence, never old ring data.
class PcmStream {
    friend class Mixer;
    std::mutex mutex_;
    unsigned rate_,channels_;
    std::vector<std::int16_t> samples_;
    std::size_t read_{},queued_{};
    std::uint64_t consumed_{},underruns_{};
    double phase_{};
    float gain_{1};
    bool playing_{},ended_{},finished_{};
    void mix(std::span<float> output,unsigned rate);
public:
    PcmStream(unsigned rate,unsigned channels,std::size_t capacity_frames=16384);
    // Returns accepted bytes, always a whole number of interleaved frames.
    std::size_t write(std::span<const std::uint8_t> pcm);
    void play();
    void pause();
    void finish_input();
    void stop();
    void gain(float amplitude);
    StreamStatus status();
};
}
