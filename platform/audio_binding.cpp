#include "audio_binding.hpp"
#include <stdexcept>

namespace wa2::platform {
AudioBinding::AudioBinding(AudioOutputFactory factory):factory_(std::move(factory)){if(!factory_)throw std::invalid_argument("Missing native audio output factory");}
AudioBinding::~AudioBinding(){try{reset();}catch(...){}}
void AudioBinding::reset(){
    // Destruction order matters even if close() throws: output first, owner
    // second. An audio callback must never retain a dangling Mixer reference.
    auto owner=std::move(mixer_);auto output=std::move(output_);paused_=false;if(output)output->close();
}
void AudioBinding::synchronize(std::shared_ptr<audio::Mixer> next,bool paused){
    if(next==mixer_){if(output_ && paused!=paused_){output_->pause(paused);paused_=paused;}return;}
    if(!next){reset();return;}
    auto prepared=factory_(*next);if(!prepared)throw std::runtime_error("Native audio output factory returned no output");
    // Never play the old and new devices together. The local shared owner
    // keeps the new mixer alive through every factory/open/failure path.
    reset();
    if(!prepared->open()){const auto error=prepared->status().error;prepared->close();throw std::runtime_error("Open native audio output: "+error);}
    prepared->pause(paused);mixer_=std::move(next);output_=std::move(prepared);paused_=paused;
}
}
