#pragma once
#include "pcm_audio.hpp"
#include "audio_levels.hpp"
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
    virtual Levels levels()const{return {};}
};
struct OggPlaylist {
    std::shared_ptr<resource::Source> first;
    std::shared_ptr<resource::Source> repeat;
};
// One bounded PCM queue and one producer per live voice. Format/header reads,
// decoder creation and packet reads stay on the producer, never the audio
// callback or game update. Decoder/I/O errors are reported through failure().
// The supplied mixer must outlive the voice; stop/destruction joins its worker.
// New voices start muted; their owner sets gain before making them audible.
std::unique_ptr<Voice> play_ogg(Mixer& mixer,OggPlaylist playlist,
    std::function<void()> initialize_worker={},bool paused=false);
std::unique_ptr<Voice> play_wave(Mixer& mixer,std::shared_ptr<resource::Source> source,bool loop,
    std::function<void()> initialize_worker={},bool paused=false);
// Preserves single-file versus _A/_B music naming in the original resource bank.
OggPlaylist music_playlist(const resource::Archive& archive,std::int32_t id,bool loop);
}
