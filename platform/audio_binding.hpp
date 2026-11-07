#pragma once
#include "audio_output.hpp"

namespace wa2::platform {
// Retain a mixer until the output and its callback have been detached. A
// scene/title handoff compares mixer owners, not only scene revision numbers.
class AudioBinding {
    AudioOutputFactory factory_;
    std::shared_ptr<audio::Mixer> mixer_;
    std::unique_ptr<AudioOutput> output_;
    bool paused_{};
public:
    explicit AudioBinding(AudioOutputFactory);
    ~AudioBinding();
    AudioBinding(const AudioBinding&)=delete;
    AudioBinding& operator=(const AudioBinding&)=delete;
    void synchronize(std::shared_ptr<audio::Mixer>,bool paused);
    void reset();
    std::shared_ptr<audio::Mixer> mixer()const noexcept{return mixer_;}
};
}
