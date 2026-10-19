#include "wave_codec.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/samplefmt.h>
}
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace wa2::audio {namespace {
unsigned word(const std::uint8_t* b){return unsigned(b[0])|(unsigned(b[1])<<8);}
std::uint32_t dword(const std::uint8_t* b){return word(b)|(std::uint32_t(word(b+2))<<16);}
AVCodecID codec_id(unsigned tag){switch(tag){case 2:return AV_CODEC_ID_ADPCM_MS;case 17:return AV_CODEC_ID_ADPCM_IMA_WAV;case 6:return AV_CODEC_ID_PCM_ALAW;case 7:return AV_CODEC_ID_PCM_MULAW;default:return AV_CODEC_ID_NONE;}}
}
WaveCodecFormat WaveCodecFormat::parse(std::span<const std::uint8_t> data){
    if(data.size()<16)throw std::invalid_argument("truncated WAVEFORMATEX");WaveCodecFormat result;
    result.tag=std::uint16_t(word(data.data()));result.channels=std::uint16_t(word(data.data()+2));result.rate=dword(data.data()+4);result.bytes_per_second=dword(data.data()+8);result.block=std::uint16_t(word(data.data()+12));result.bits=std::uint16_t(word(data.data()+14));
    if(data.size()>=18){const auto extra=word(data.data()+16);if(extra>4096 || extra>data.size()-18)throw std::invalid_argument("truncated WAVEFORMATEX extension");result.extra.assign(data.begin()+18,data.begin()+18+extra);}
    return result;
}
bool WaveCodecFormat::supported()const {
    if(channels<1 || channels>2 || rate<1000 || rate>384000 || !block)return false;
    if(tag==1)return (bits==8 || bits==16) && block==channels*(bits/8) && bytes_per_second==rate*block;
    if(tag==6 || tag==7)return bits==8 && block==channels;
    if(tag==2){if(bits!=4 || block<7*channels || extra.size()<4)return false;const auto coefficients=word(extra.data()+2);
        return coefficients>0 && coefficients<=256 && extra.size()>=4+coefficients*4 && word(extra.data())==((block-7*channels)*2/channels+2);}
    if(tag==17)return bits==4 && block>=4*channels && extra.size()>=2 && word(extra.data())==((block-4*channels)*2/channels+1);
    return false;
}
unsigned WaveCodecFormat::block_frames()const {if(!supported())throw std::invalid_argument("unsupported WAVE block format");return tag==2 || tag==17?word(extra.data()):1;}
struct WaveBlockDecoder::Impl {
    WaveCodecFormat format;AVCodecContext* context{};AVPacket* packet{};AVFrame* frame{};
    ~Impl(){av_frame_free(&frame);av_packet_free(&packet);avcodec_free_context(&context);}
};
WaveBlockDecoder::WaveBlockDecoder(WaveCodecFormat format):impl_(std::make_unique<Impl>()){
    if(!format.supported())throw std::invalid_argument("unsupported native WAVE codec");impl_->format=std::move(format);auto& self=*impl_;
    if(self.format.tag==1)return;const auto* codec=avcodec_find_decoder(codec_id(self.format.tag));if(!codec)throw std::runtime_error("required native WAVE decoder is not built");
    self.context=avcodec_alloc_context3(codec);self.packet=av_packet_alloc();self.frame=av_frame_alloc();if(!self.context || !self.packet || !self.frame)throw std::bad_alloc();
    auto* context=self.context;context->sample_rate=int(self.format.rate);context->block_align=self.format.block;context->bits_per_coded_sample=self.format.bits;context->thread_count=1;av_channel_layout_default(&context->ch_layout,self.format.channels);
    if(!self.format.extra.empty()){context->extradata=static_cast<std::uint8_t*>(av_mallocz(self.format.extra.size()+AV_INPUT_BUFFER_PADDING_SIZE));if(!context->extradata)throw std::bad_alloc();std::copy(self.format.extra.begin(),self.format.extra.end(),context->extradata);context->extradata_size=int(self.format.extra.size());}
    if(avcodec_open2(context,codec,nullptr)<0)throw std::runtime_error("cannot initialize native WAVE decoder");
}
WaveBlockDecoder::~WaveBlockDecoder()=default;
void WaveBlockDecoder::reset(){if(impl_->context)avcodec_flush_buffers(impl_->context);}
std::vector<std::uint8_t> WaveBlockDecoder::decode(std::span<const std::uint8_t> block){
    auto& self=*impl_;if(block.empty() || block.size()>65536 || block.size()%self.format.block)throw std::invalid_argument("unaligned native WAVE block");
    if(self.format.tag==1){if(self.format.bits==16)return {block.begin(),block.end()};std::vector<std::uint8_t> pcm(block.size()*2);
        for(std::size_t n=0;n<block.size();++n){const auto sample=std::uint16_t((int(block[n])-128)*256);pcm[n*2]=std::uint8_t(sample);pcm[n*2+1]=std::uint8_t(sample>>8);}return pcm;}
    if((self.format.tag==2 || self.format.tag==17) && block.size()!=self.format.block)throw std::invalid_argument("one ADPCM block is required");
    av_packet_unref(self.packet);if(av_new_packet(self.packet,int(block.size()))<0)throw std::bad_alloc();std::memcpy(self.packet->data,block.data(),block.size());
    if(avcodec_send_packet(self.context,self.packet)<0)throw std::runtime_error("invalid native WAVE packet");std::vector<std::uint8_t> pcm;
    for(;;){const auto result=avcodec_receive_frame(self.context,self.frame);if(result==AVERROR(EAGAIN) || result==AVERROR_EOF)break;if(result<0)throw std::runtime_error("invalid native WAVE frame");
        const auto* frame=self.frame;const auto planar=frame->format==AV_SAMPLE_FMT_S16P;if(frame->format!=AV_SAMPLE_FMT_S16 && !planar)throw std::runtime_error("unsupported native WAVE decoder output");
        if(frame->sample_rate!=int(self.format.rate) || frame->ch_layout.nb_channels!=self.format.channels || frame->nb_samples<0 || frame->nb_samples>262144)throw std::runtime_error("native WAVE output dimensions changed");
        const auto first=pcm.size(),bytes=std::size_t(frame->nb_samples)*self.format.channels*2;if(first+bytes>1024*1024)throw std::runtime_error("native WAVE decoded-block limit");pcm.resize(first+bytes);
        if(!planar)std::memcpy(pcm.data()+first,frame->data[0],bytes);
        else for(unsigned n=0;n<unsigned(frame->nb_samples);++n)for(unsigned c=0;c<self.format.channels;++c)std::memcpy(pcm.data()+first+(std::size_t(n)*self.format.channels+c)*2,frame->data[c]+n*2,2);
        av_frame_unref(self.frame);
    }
    return pcm;
}
}
