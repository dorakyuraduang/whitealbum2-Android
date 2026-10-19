#include "pcm_stream.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace wa2::audio {
PcmStream::PcmStream(unsigned rate,unsigned channels,std::size_t capacity)
    :rate_(rate),channels_(channels){
    if(rate<1000 || rate>384000 || channels<1 || channels>2 || capacity<2 || capacity>262144)
        throw std::invalid_argument("invalid native PCM stream dimensions");
    samples_.resize(capacity*channels);
}
std::size_t PcmStream::write(std::span<const std::uint8_t> pcm){
    const auto frame_bytes=channels_*2;
    if(pcm.size()%frame_bytes)throw std::invalid_argument("unaligned native PCM stream input");
    std::lock_guard lock(mutex_);
    if(ended_ || finished_)throw std::logic_error("native PCM input after stream end");
    const auto capacity=samples_.size()/channels_;
    const auto frames=std::min(pcm.size()/frame_bytes,capacity-queued_);
    for(std::size_t frame=0;frame<frames;++frame){
        const auto at=((read_+queued_+frame)%capacity)*channels_;
        for(unsigned c=0;c<channels_;++c){const auto offset=frame*frame_bytes+c*2;
            samples_[at+c]=std::bit_cast<std::int16_t>(std::uint16_t(unsigned(pcm[offset])|(unsigned(pcm[offset+1])<<8)));}
    }
    queued_+=frames;return frames*frame_bytes;
}
void PcmStream::finish_input(){std::lock_guard lock(mutex_);ended_=true;if(!queued_){finished_=true;playing_=false;}}
void PcmStream::play(){std::lock_guard lock(mutex_);if(!finished_)playing_=true;}
void PcmStream::pause(){std::lock_guard lock(mutex_);playing_=false;}
void PcmStream::stop(){std::lock_guard lock(mutex_);playing_=false;finished_=true;ended_=true;queued_=0;phase_=0;}
void PcmStream::gain(float amplitude){
    if(!std::isfinite(amplitude) || amplitude<0 || amplitude>1)throw std::invalid_argument("invalid native PCM gain");
    std::lock_guard lock(mutex_);gain_=amplitude;
}
StreamStatus PcmStream::status(){std::lock_guard lock(mutex_);
    return {queued_,samples_.size()/channels_-queued_,consumed_,underruns_,playing_,finished_,ended_};}
void PcmStream::mix(std::span<float> output,unsigned output_rate){
    std::lock_guard lock(mutex_);if(!playing_)return;
    const auto capacity=samples_.size()/channels_;const double step=double(rate_)/output_rate;
    bool starved=false;
    auto consume=[&]{
        const auto n=std::min(std::size_t(phase_),queued_);
        read_=(read_+n)%capacity;queued_-=n;consumed_+=n;phase_-=double(n);
    };
    for(std::size_t n=0;n+1<output.size();n+=2){
        consume();
        if(!queued_){if(ended_){finished_=true;playing_=false;}else starved=true;break;}
        if(phase_>=1){starved=true;break;} // Input skipped by a downsampling step is still due.
        if(phase_>0 && queued_<2 && !ended_){starved=true;break;}
        const auto next=(read_+(queued_>1?1:0))%capacity;const auto fraction=float(phase_);
        for(unsigned c=0;c<2;++c){const auto channel=std::min(c,channels_-1);
            const float a=samples_[read_*channels_+channel]/32768.f,b=samples_[next*channels_+channel]/32768.f;
            output[n+c]+=(a+(b-a)*fraction)*gain_;
        }
        phase_+=step;
    }
    consume();
    if(ended_ && !queued_){finished_=true;playing_=false;}
    if(starved)++underruns_;
}
}
