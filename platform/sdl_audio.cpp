#include "sdl_audio.hpp"
#include <SDL.h>
#include <atomic>
#include <cstring>

namespace wa2::platform {
struct SdlAudio::Impl {
    audio::Mixer& mixer;
    SDL_AudioDeviceID device{};
    SDL_AudioSpec spec{};
    bool initialized{};
    std::string error;
    std::atomic<std::uint64_t> callbacks{},frames{},errors{};
    explicit Impl(audio::Mixer& value):mixer(value){}
    bool init(){if(initialized)return true;if(SDL_InitSubSystem(SDL_INIT_AUDIO)!=0){error=SDL_GetError();return false;}initialized=true;return true;}
    static void callback(void* opaque,Uint8* stream,int size) noexcept {
        auto& self=*static_cast<Impl*>(opaque);
        try{self.mixer.render(std::span(reinterpret_cast<float*>(stream),std::size_t(size)/sizeof(float)),unsigned(self.spec.freq));
            self.frames.fetch_add(std::size_t(size)/(sizeof(float)*2),std::memory_order_relaxed);
            self.callbacks.fetch_add(1,std::memory_order_relaxed);
        }catch(...){std::memset(stream,0,std::size_t(size));self.errors.fetch_add(1,std::memory_order_relaxed);}
    }
};
SdlAudio::SdlAudio(audio::Mixer& mixer):impl_(std::make_unique<Impl>(mixer)){}
SdlAudio::~SdlAudio(){close();}
bool SdlAudio::open(const std::string& name){
    auto& s=*impl_;if(s.device)return true;if(!s.init())return false;
    SDL_AudioSpec desired{};desired.freq=48000;desired.format=AUDIO_F32SYS;desired.channels=2;desired.samples=512;desired.callback=Impl::callback;desired.userdata=&s;
    s.device=SDL_OpenAudioDevice(name.empty()?nullptr:name.c_str(),0,&desired,&s.spec,SDL_AUDIO_ALLOW_FREQUENCY_CHANGE|SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if(!s.device){s.error=SDL_GetError();return false;}s.error.clear();SDL_PauseAudioDevice(s.device,0);return true;
}
std::vector<std::string> SdlAudio::devices(){std::vector<std::string> result;if(!impl_->init())return result;const auto count=SDL_GetNumAudioDevices(0);for(int n=0;n<count;++n)if(const auto* name=SDL_GetAudioDeviceName(n,0))result.emplace_back(name);return result;}
void SdlAudio::close(){auto& s=*impl_;if(s.device){SDL_CloseAudioDevice(s.device);s.device=0;}if(s.initialized){SDL_QuitSubSystem(SDL_INIT_AUDIO);s.initialized=false;}}
void SdlAudio::pause(bool paused){if(impl_->device)SDL_PauseAudioDevice(impl_->device,paused?1:0);}
AudioStatus SdlAudio::status() const {const auto& s=*impl_;const auto* driver=s.initialized?SDL_GetCurrentAudioDriver():nullptr;return {unsigned(s.spec.freq),s.spec.samples,s.callbacks.load(),s.frames.load(),s.errors.load(),driver?driver:"",s.error};}
}
