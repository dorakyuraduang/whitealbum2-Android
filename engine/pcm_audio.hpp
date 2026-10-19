#pragma once
// Native game audio. This module has no Windows, guest CPU/address-space, COM
// or SDL dependency. Platforms supply stereo output frames to Mixer::render.
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace wa2::audio {
class PcmStream;
enum class Encoding { unsigned8, signed16, signed24, signed32, float32 };
struct Format {
    std::uint32_t rate{48000};
    std::uint16_t channels{2};
    Encoding encoding{Encoding::signed16};
    unsigned sample_bytes() const;
    unsigned frame_bytes() const {return channels*sample_bytes();}
    bool valid() const;
};
struct Position {
    std::uint32_t play{},write{},frequency{};
    int volume{},pan{};
    bool playing{},looping{};
};
struct Notification {
    static constexpr std::uint32_t stopped=0xffffffffu;
    std::uint32_t offset{};
    // Runs on the output thread. Must be short, thread safe and must not call
    // back into the mixer. Event objects are captured by shared/weak ownership.
    std::function<void()> signal;
};
class Buffer {
    struct Samples {std::mutex mutex;std::vector<std::uint8_t> bytes;};
    mutable std::mutex mutex_;
    std::shared_ptr<Samples> samples_;
    Format format_;
    std::uint32_t frequency_;
    double frame_{};
    int volume_{},pan_{};
    float left_{1},right_{1};
    bool playing_{},looping_{};
    std::vector<Notification> notifications_;
    Buffer(Format,std::shared_ptr<Samples>);
    void update_gain();
    void signal_stopped();
    friend class Mixer;
    void mix(std::span<float> stereo,unsigned rate);
public:
    Buffer(Format,std::uint32_t bytes);
    std::uint32_t bytes() const {return std::uint32_t(samples_->bytes.size());}
    Format format() const;
    Position position(unsigned write_ahead_frames=1024,unsigned output_rate=48000) const;
    void play(bool loop);
    void stop();
    bool seek(std::uint32_t byte);
    bool volume(int hundredths_db);
    bool pan(int hundredths_db);
    bool frequency(std::uint32_t hz); // zero restores the source rate
    bool notifications(std::vector<Notification>);
    // Edits are serialized with rendering, and never need a third PCM copy.
    // Callback spans may not escape the call.
    void read(std::uint32_t offset,std::span<std::uint8_t> destination) const;
    void edit(std::uint32_t offset,std::uint32_t bytes,const std::function<void(std::span<std::uint8_t>)>&);
};
class Mixer {
    mutable std::mutex mutex_;
    std::vector<std::weak_ptr<Buffer>> buffers_;
    std::vector<std::weak_ptr<PcmStream>> streams_;
public:
    std::shared_ptr<Buffer> create(Format,std::uint32_t bytes);
    std::shared_ptr<Buffer> duplicate(const std::shared_ptr<Buffer>&);
    std::shared_ptr<PcmStream> create_stream(unsigned rate,unsigned channels,std::size_t capacity_frames=16384);
    void render(std::span<float> interleaved_stereo,unsigned rate);
    std::size_t live_buffers() const;
};
}
