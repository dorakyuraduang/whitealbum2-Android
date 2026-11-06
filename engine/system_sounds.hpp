#pragma once
#include "pcm_audio.hpp"
#include "resource_archive.hpp"
#include <array>

namespace wa2::audio {
enum class SystemCue {focus,accept,cancel,reject,message};
struct SoundResource {std::shared_ptr<resource::Source> source;bool ogg{};};
// The original five UI cues have persistent, independent PCM buffers. Loading
// is explicit; play() performs no resource access, decoding or thread creation.
class SystemSounds {
public:
    using Loader=std::function<SoundResource(std::int32_t)>;
    SystemSounds(Mixer&,Loader);
    void prepare();
    void play(SystemCue);
    void stop();
    void settings(std::int32_t master,std::int32_t system,bool enabled=true);
    bool ready()const noexcept{return bool(buffers_[0]);}
    std::size_t pcm_bytes()const noexcept;
private:
    Mixer& mixer_;Loader loader_;
    std::array<std::shared_ptr<Buffer>,5> buffers_;
    std::int32_t master_{256},system_{256};bool enabled_{true};
    int attenuation()const;
};
}
