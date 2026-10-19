#pragma once
#include "movie_reader.hpp"
#include "pcm_audio.hpp"

namespace wa2::movie{
class Playback{
public:
    virtual ~Playback()=default;
    virtual bool finished()const=0;
    virtual void stop()=0;
    virtual void update(std::int64_t monotonic_us)=0;
    virtual void pause(bool value,std::int64_t monotonic_us)=0;
    virtual void gain(float amplitude)=0;
    virtual std::optional<VideoFrame> frame()const=0;
};
struct PlayerStatistics{
    ReaderStatistics reader;
    std::size_t queued_video_frames{},peak_video_frames{},queued_audio_frames{};
    std::uint64_t audio_underruns{},skipped_video_frames{};
    bool ready{},started{},decode_finished{},finished{};
};
// One decoder worker, at most four queued YUV frames and 16384 PCM frames.
// All rendering and clock updates stay on the host/render thread.
class Player final:public Playback{
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Player(std::shared_ptr<resource::Source>,std::shared_ptr<audio::Mixer>,std::function<void()> worker_initializer={});
    ~Player();
    Player(const Player&)=delete;
    Player& operator=(const Player&)=delete;
    void update(std::int64_t monotonic_us)override;
    void pause(bool value,std::int64_t monotonic_us)override;
    void gain(float amplitude)override;
    std::optional<VideoFrame> frame()const override;
    bool finished()const override;
    void stop()override;
    PlayerStatistics statistics()const;
};
}
