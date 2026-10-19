#pragma once
#include "resource_archive.hpp"
#include <array>
#include <functional>
#include <variant>

namespace wa2::movie {
class Error:public std::runtime_error{using std::runtime_error::runtime_error;};
enum class ColourMatrix{bt601,bt709};
struct Plane{const std::uint8_t* pixels{};int pitch{},width{},height{};};
struct VideoFrame{
    // References the decoder's native frame buffers. The renderer does not
    // need an intermediate RGBA image or a copy of the complete movie.
    std::shared_ptr<const void> owner;
    std::array<Plane,3> planes;
    int width{},height{};
    int aspect_numerator{1},aspect_denominator{1};
    std::int64_t time_us{},duration_us{};
    ColourMatrix matrix{ColourMatrix::bt601};
    bool full_range{};
};
struct AudioFrame{
    std::vector<std::uint8_t> pcm16;
    unsigned rate{},channels{};
    std::int64_t time_us{};
};
using Frame=std::variant<VideoFrame,AudioFrame>;
enum class Track{any,video,audio};
struct Information{
    unsigned width{},height{},audio_rate{},audio_channels{};
    std::int64_t duration_us{};
};
struct ReaderStatistics{
    std::uint64_t bytes_read{},read_calls{},seek_calls{},packets{},video_frames{},audio_frames{};
    std::size_t largest_read{};
    std::size_t queued_packet_bytes{},peak_packet_bytes{},peak_packet_count{};
};
// Sequential, bounded decoding from the original seekable file/descriptor.
// No URLs, platform movie service, CPU interpreter or resource extraction.
class Reader{
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    explicit Reader(std::shared_ptr<resource::Source> source,std::function<bool()> cancelled={});
    ~Reader();
    Reader(const Reader&)=delete;
    Reader& operator=(const Reader&)=delete;
    const Information& information()const noexcept;
    ReaderStatistics statistics()const noexcept;
    // Selecting a track buffers only the other track's compressed packets.
    // This handles ASF files with several seconds of video before audio data
    // without retaining several seconds of decoded full-size pictures.
    std::optional<Frame> next(Track preference=Track::any);
};
}
