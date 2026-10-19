#pragma once
#include "pcm_audio.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace wa2::platform {
struct AudioStatus {unsigned rate{},frames{};std::uint64_t callbacks{},rendered_frames{},errors{};std::string driver,error;};
class AudioOutput {
public:
    virtual ~AudioOutput()=default;
    virtual bool open(const std::string& device={})=0;
    virtual std::vector<std::string> devices()=0;
    virtual void close()=0;
    virtual void pause(bool)=0;
    virtual AudioStatus status() const=0;
};
using AudioOutputFactory=std::function<std::unique_ptr<AudioOutput>(audio::Mixer&)>;
}
