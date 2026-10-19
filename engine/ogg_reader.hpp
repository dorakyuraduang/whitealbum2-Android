#pragma once
#include "resource_archive.hpp"
#include "vorbis_stream.hpp"
#include <array>

namespace wa2::audio {
// Pulls PCM from one seekable original-archive member. Compressed input is read
// in 8 KiB pieces and PCM is delivered in caller-owned, frame-aligned spans.
class OggReader {
    std::shared_ptr<resource::Source> source_;
    std::unique_ptr<VorbisStream> decoder_;
    VorbisFormat format_;
    std::array<std::uint8_t,8192> input_{};
    std::size_t begin_{},end_{};
    std::uint64_t offset_{};
    bool finished_{},need_input_{true};
public:
    explicit OggReader(std::shared_ptr<resource::Source> source);
    VorbisFormat format()const noexcept{return format_;}
    std::size_t read(std::span<std::uint8_t> pcm);
    bool finished()const noexcept{return finished_;}
    void rewind();
    std::size_t buffered_bytes()const noexcept;
};
}
