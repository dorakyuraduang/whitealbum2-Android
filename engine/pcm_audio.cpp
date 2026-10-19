#include "pcm_audio.hpp"
#include "pcm_stream.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace wa2::audio {
unsigned Format::sample_bytes() const {
    switch(encoding){case Encoding::unsigned8:return 1;case Encoding::signed16:return 2;case Encoding::signed24:return 3;default:return 4;}
}
bool Format::valid() const {return rate>=100 && rate<=200000 && channels>=1 && channels<=2;}
Buffer::Buffer(Format format,std::uint32_t bytes):samples_(std::make_shared<Samples>()),format_(format),frequency_(format.rate){
    if(!format.valid() || !bytes || bytes%format.frame_bytes())throw std::invalid_argument("Invalid PCM buffer dimensions");
    samples_->bytes.assign(bytes,format.encoding==Encoding::unsigned8?128:0);
}
Buffer::Buffer(Format format,std::shared_ptr<Samples> samples):samples_(std::move(samples)),format_(format),frequency_(format.rate){}
Format Buffer::format() const {std::lock_guard lock(mutex_);return format_;}
Position Buffer::position(unsigned write_ahead_frames,unsigned output_rate) const {
    std::lock_guard lock(mutex_);const auto align=format_.frame_bytes(),length=bytes();
    const auto play=std::uint32_t(frame_)*align;
    const auto ahead=output_rate?std::uint32_t((std::uint64_t(write_ahead_frames)*frequency_+output_rate-1)/output_rate)*align:0;
    return {play%length,playing_?std::uint32_t((std::uint64_t(play)+ahead)%length):play%length,frequency_,volume_,pan_,playing_,looping_};
}
void Buffer::play(bool loop){std::lock_guard lock(mutex_);looping_=loop;playing_=true;}
void Buffer::signal_stopped(){for(const auto& n:notifications_)if(n.offset==Notification::stopped && n.signal)n.signal();}
void Buffer::stop(){std::lock_guard lock(mutex_);if(playing_){playing_=false;signal_stopped();}}
bool Buffer::seek(std::uint32_t byte){std::lock_guard lock(mutex_);if(byte>=bytes())return false;frame_=byte/format_.frame_bytes();return true;}
void Buffer::update_gain(){
    const auto amplitude=[](int v){return v<=-10000?0.f:float(std::pow(10.,v/2000.));};
    left_=amplitude(volume_)*amplitude(-std::max(pan_,0));right_=amplitude(volume_)*amplitude(std::min(pan_,0));
}
bool Buffer::volume(int value){if(value<-10000 || value>0)return false;std::lock_guard lock(mutex_);if(value!=volume_){volume_=value;update_gain();}return true;}
bool Buffer::pan(int value){if(value<-10000 || value>10000)return false;std::lock_guard lock(mutex_);if(value!=pan_){pan_=value;update_gain();}return true;}
bool Buffer::frequency(std::uint32_t value){std::lock_guard lock(mutex_);if(value && (value<100 || value>200000))return false;frequency_=value?value:format_.rate;return true;}
bool Buffer::notifications(std::vector<Notification> value){
    if(std::any_of(value.begin(),value.end(),[&](const auto& n){return !n.signal || (n.offset!=Notification::stopped && n.offset>=bytes());}))return false;
    std::lock_guard lock(mutex_);if(playing_)return false;notifications_=std::move(value);return true;
}
void Buffer::read(std::uint32_t offset,std::span<std::uint8_t> destination) const {
    if(offset>bytes() || destination.size()>bytes()-offset)throw std::out_of_range("PCM read exceeds buffer");
    std::lock_guard lock(samples_->mutex);std::copy_n(samples_->bytes.data()+offset,destination.size(),destination.data());
}
void Buffer::edit(std::uint32_t offset,std::uint32_t count,const std::function<void(std::span<std::uint8_t>)>& fn){
    if(offset>bytes() || count>bytes()-offset)throw std::out_of_range("PCM upload exceeds buffer");
    std::lock_guard lock(samples_->mutex);fn(std::span(samples_->bytes).subspan(offset,count));
}
namespace {
void add_pcm16(const std::uint8_t* source,std::span<float> output,unsigned channels,float left,float right){
    const auto sample=[](const std::uint8_t* at){return std::bit_cast<std::int16_t>(std::uint16_t(at[0]|(unsigned(at[1])<<8)))*(1.f/32768.f);};
    if(channels==2){
        for(std::size_t n=0;n<output.size()/2;++n){output[n*2]+=sample(source+n*4)*left;output[n*2+1]+=sample(source+n*4+2)*right;}
    }else{
        for(std::size_t n=0;n<output.size()/2;++n){const auto value=sample(source+n*2);output[n*2]+=value*left;output[n*2+1]+=value*right;}
    }
}
float decode(const std::uint8_t* b,Encoding encoding){
    if(encoding==Encoding::unsigned8)return (int(*b)-128)/128.f;
    if(encoding==Encoding::signed16)return std::bit_cast<std::int16_t>(std::uint16_t(b[0]|(unsigned(b[1])<<8)))/32768.f;
    if(encoding==Encoding::signed24){const auto v=std::uint32_t(b[0])|(std::uint32_t(b[1])<<8)|(std::uint32_t(b[2])<<16);return std::bit_cast<std::int32_t>(v|((v&0x800000)?0xff000000u:0))/8388608.f;}
    const auto v=std::uint32_t(b[0])|(std::uint32_t(b[1])<<8)|(std::uint32_t(b[2])<<16)|(std::uint32_t(b[3])<<24);
    if(encoding==Encoding::signed32)return float(std::bit_cast<std::int32_t>(v)/2147483648.);
    const auto sample=std::bit_cast<float>(v);return std::isfinite(sample)?sample:0;
}
}
void Buffer::mix(std::span<float> output,unsigned output_rate){
    std::lock_guard lock(mutex_);if(!playing_)return;
    std::lock_guard data_lock(samples_->mutex);
    const auto channels=format_.channels;const auto sample_bytes=format_.sample_bytes(),align=format_.frame_bytes(),frames=bytes()/align;
    const auto* data=samples_->bytes.data();const double advance=double(frequency_)/output_rate,start=frame_;double travelled=0;bool ended=false;
    // An integer cursor at the output rate needs no resampling. In the
    // signed-16 path the discarded interpolation term is exactly signed zero;
    // preserve the same sample multiply/add order and notification interval.
    if(format_.encoding==Encoding::signed16 && frequency_==output_rate && frame_==std::uint32_t(frame_)){
        std::size_t done{};
        while(done<output.size()/2){
            const auto index=std::uint32_t(frame_);
            const auto count=std::min<std::size_t>(output.size()/2-done,frames-index);
            add_pcm16(data+std::size_t(index)*align,output.subspan(done*2,count*2),channels,left_,right_);
            done+=count;frame_+=double(count);travelled+=double(count);
            if(frame_>=frames){if(looping_)frame_=0;else{frame_=0;playing_=false;ended=true;break;}}
        }
    }else for(std::size_t n=0;n+1<output.size();n+=2){
        const auto index=std::uint32_t(frame_),next=index+1<frames?index+1:looping_?0:index;
        const auto fraction=float(frame_-index);
        for(unsigned channel=0;channel<2;++channel){const auto part=std::min<unsigned>(channel,channels-1)*sample_bytes;
            const auto a=decode(data+index*align+part,format_.encoding),b=decode(data+next*align+part,format_.encoding);
            output[n+channel]+=(a+(b-a)*fraction)*(channel?right_:left_);
        }
        const auto step=looping_?advance:std::min(advance,frames-frame_);travelled+=step;frame_+=step;
        if(frame_>=frames){if(looping_)frame_=std::fmod(frame_,double(frames));else{frame_=0;playing_=false;ended=true;break;}}
    }
    // Test byte crossings against an unwrapped interval, so a callback which
    // crosses the end or more than one loop cannot lose its notification.
    const double start_byte=start*align,delta=travelled*align,length=bytes();
    for(const auto& n:notifications_)if(n.offset!=Notification::stopped){
        double distance=n.offset-start_byte;if(distance<0)distance+=length;
        if(delta>0 && distance<delta && n.signal)n.signal();
    }
    if(ended)signal_stopped();
}
std::shared_ptr<Buffer> Mixer::create(Format format,std::uint32_t bytes){
    auto buffer=std::make_shared<Buffer>(format,bytes);std::lock_guard lock(mutex_);
    std::erase_if(buffers_,[](const auto& b){return b.expired();});buffers_.push_back(buffer);return buffer;
}
std::shared_ptr<Buffer> Mixer::duplicate(const std::shared_ptr<Buffer>& source){
    std::shared_ptr<Buffer> result;{
        std::lock_guard source_lock(source->mutex_);result=std::shared_ptr<Buffer>(new Buffer(source->format_,source->samples_));
        result->frequency_=source->frequency_;result->volume_=source->volume_;result->pan_=source->pan_;result->update_gain();
    }
    std::lock_guard lock(mutex_);std::erase_if(buffers_,[](const auto& b){return b.expired();});buffers_.push_back(result);return result;
}
void Mixer::render(std::span<float> output,unsigned rate){
    if(!rate || output.size()%2)throw std::invalid_argument("Invalid stereo output dimensions");
    std::fill(output.begin(),output.end(),0.f);std::lock_guard lock(mutex_);
    for(auto it=buffers_.begin();it!=buffers_.end();)if(auto buffer=it->lock()){buffer->mix(output,rate);++it;}else it=buffers_.erase(it);
    for(auto it=streams_.begin();it!=streams_.end();)if(auto stream=it->lock()){stream->mix(output,rate);++it;}else it=streams_.erase(it);
    for(auto& sample:output)sample=std::clamp(sample,-1.f,1.f);
}
std::shared_ptr<PcmStream> Mixer::create_stream(unsigned rate,unsigned channels,std::size_t capacity){
    auto stream=std::make_shared<PcmStream>(rate,channels,capacity);std::lock_guard lock(mutex_);
    std::erase_if(streams_,[](const auto& value){return value.expired();});streams_.push_back(stream);return stream;
}
std::size_t Mixer::live_buffers() const {std::lock_guard lock(mutex_);return std::count_if(buffers_.begin(),buffers_.end(),[](const auto& b){return !b.expired();});}
}
