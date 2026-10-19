#pragma once
#include "../platform/audio_output.hpp"
namespace wr {
class Process;
struct SharedAudio {
    std::shared_ptr<wa2::audio::Mixer> mixer;
    std::function<bool()> open_output;
};
SharedAudio install_wa2_audio_service(Process&,const wa2::platform::AudioOutputFactory& factory={});
void install_wa2_audio(Process&);
void install_wa2_audio(Process&,const wa2::platform::AudioOutputFactory&);
}
