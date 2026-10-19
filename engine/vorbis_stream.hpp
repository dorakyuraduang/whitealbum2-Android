#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
namespace wa2::audio {
class VorbisError:public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct VorbisFormat {unsigned channels{},rate{};bool operator==(const VorbisFormat&) const=default;};
// Read the identification page only. No decoder, PCM buffer, or OS resource is
// created; callers can ask for a format before all setup packets are available.
VorbisFormat query_vorbis_format(std::span<const std::uint8_t> identification_page);
class VorbisStream {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    struct Result {std::size_t bytes{};bool finished{};unsigned channels{},rate{};std::size_t accepted{};};
    VorbisStream();~VorbisStream();
    VorbisStream(const VorbisStream&)=delete;VorbisStream& operator=(const VorbisStream&)=delete;
    // Partial acceptance provides backpressure when output is much smaller.
    Result push(std::span<const std::uint8_t> input,std::span<std::uint8_t> output,bool end_of_input=false);
    std::size_t buffered_bytes() const noexcept;
};
}
