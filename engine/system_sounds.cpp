#include "system_sounds.hpp"
#include "ogg_reader.hpp"
#include "wave_reader.hpp"
#include <algorithm>
#include <cmath>

namespace wa2::audio {
namespace {
constexpr std::size_t maximum_pcm=4u*1024*1024;
template<class Reader>std::shared_ptr<Buffer> decode(Mixer& mixer,SoundResource resource){
    Reader reader(std::move(resource.source));const auto format=reader.format();
    const Format pcm_format{format.rate,std::uint16_t(format.channels),Encoding::signed16};
    if(!pcm_format.valid())throw resource::Error("Unsupported system sound PCM format");
    std::vector<std::uint8_t> pcm;std::array<std::uint8_t,8192> block{};
    while(!reader.finished()){
        const auto count=reader.read(block);
        if(!count && !reader.finished())throw resource::Error("System sound decoder made no progress");
        if(count>maximum_pcm-pcm.size())throw resource::Error("System sound exceeds the 4 MiB PCM limit");
        pcm.insert(pcm.end(),block.begin(),block.begin()+count);
    }
    if(pcm.empty() || pcm.size()%pcm_format.frame_bytes())throw resource::Error("System sound contains no aligned PCM frames");
    auto buffer=mixer.create(pcm_format,std::uint32_t(pcm.size()));
    buffer->edit(0,buffer->bytes(),[&](auto output){std::copy(pcm.begin(),pcm.end(),output.begin());});return buffer;
}
}
SystemSounds::SystemSounds(Mixer& mixer,Loader loader):mixer_(mixer),loader_(std::move(loader)){
    if(!loader_)throw std::invalid_argument("Missing system sound resource loader");
}
int SystemSounds::attenuation()const{
    // Cached 405aa0 scales the two 0..256 sliders before 45fc10 converts the
    // resulting byte to DirectSound hundredths of a dB. Device trim is unity.
    const auto volume=std::min(255,master_*system_/256);
    if(!volume)return -10000;
    const auto amplitude=float(double(volume)/255.);
    const auto logarithm=float(std::log10(double(amplitude)));
    return std::clamp(int(double(logarithm)*2000.),-10000,0);
}
void SystemSounds::prepare(){
    if(ready())return;
    constexpr std::array<std::int32_t,5> ids{9211,9212,9213,9214,9015};
    decltype(buffers_) pending;
    for(unsigned n=0;n<ids.size();++n){auto resource=loader_(ids[n]);
        if(!resource.source)throw resource::Error("Missing system sound "+std::to_string(ids[n]));
        pending[n]=resource.ogg?decode<OggReader>(mixer_,std::move(resource)):decode<WaveReader>(mixer_,std::move(resource));
        pending[n]->volume(attenuation());
    }
    buffers_=std::move(pending);
}
void SystemSounds::play(SystemCue cue){
    const auto index=std::size_t(cue);
    if(index>=buffers_.size())throw std::invalid_argument("Unknown system sound cue");
    if(!ready())throw std::logic_error("System sounds have not been prepared");
    if(!enabled_ || !master_ || !system_)return;
    auto& buffer=buffers_[index];buffer->stop();buffer->seek(0);buffer->play(false);
}
void SystemSounds::stop(){for(auto& buffer:buffers_)if(buffer)buffer->stop();}
void SystemSounds::settings(std::int32_t master,std::int32_t system,bool enabled){
    if(master<0 || master>256 || system<0 || system>256)throw std::invalid_argument("System sound volume is out of range");
    master_=master;system_=system;enabled_=enabled;
    for(auto& buffer:buffers_)if(buffer)buffer->volume(attenuation());
    if(!enabled_ || !master_ || !system_)stop();
}
std::size_t SystemSounds::pcm_bytes()const noexcept{std::size_t bytes{};for(const auto& buffer:buffers_)if(buffer)bytes+=buffer->bytes();return bytes;}
}
