#pragma once
#include "audio_output.hpp"
#include <memory>
#include <string>
#include <vector>

namespace wa2::platform {
class SdlAudio final:public AudioOutput {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    explicit SdlAudio(audio::Mixer&);
    ~SdlAudio() override;
    SdlAudio(const SdlAudio&)=delete;
    SdlAudio& operator=(const SdlAudio&)=delete;
    bool open(const std::string& device={}) override;
    std::vector<std::string> devices() override;
    void close() override;
    void pause(bool paused) override;
    AudioStatus status() const override;
};
}
