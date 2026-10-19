#pragma once
#include "pcm_audio.hpp"
#include "resource_archive.hpp"
#include <functional>
#include <string>

namespace wa2::audio {
class Voice {
public:
    virtual ~Voice()=default;
    virtual bool ready()const=0;
    virtual bool finished()const=0;
    virtual std::string failure()const=0;
    virtual void gain(std::uint8_t volume)=0;
    virtual void stop()=0;
    virtual void pause(bool){throw resource::Error("audio voice does not support preparation/pause");}
};
struct OggPlaylist {
    std::shared_ptr<resource::Source> first;
    std::shared_ptr<resource::Source> repeat;
};
// One bounded PCM queue and one producer per live voice. Only the 58-byte format
// header is read during creation; packet decoding and subsequent disk reads
// stay on the producer, never the audio callback or game update.
// New voices start muted; their owner sets gain before making them audible.
std::unique_ptr<Voice> play_ogg(Mixer& mixer,OggPlaylist playlist,
    std::function<void()> initialize_worker={},bool paused=false);
std::unique_ptr<Voice> play_wave(Mixer& mixer,std::shared_ptr<resource::Source> source,bool loop,
    std::function<void()> initialize_worker={},bool paused=false);
// Preserves single-file versus _A/_B music naming in the original resource bank.
OggPlaylist music_playlist(const resource::Archive& archive,std::int32_t id,bool loop);
}
