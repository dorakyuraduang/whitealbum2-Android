#include "movie_reader.hpp"
#include <algorithm>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
extern "C"{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace wa2::movie{
namespace{
constexpr AVRational microseconds{1,1000000};
[[noreturn]]void fail(const char* action,int error){
    char message[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(error,message,sizeof(message));
    throw Error(std::string(action)+": "+message);
}
struct Codec{
    AVCodecContext* context{};
    AVStream* stream{};
    bool flushed{};
    bool draining{};
    ~Codec(){avcodec_free_context(&context);}
};
struct ChannelLayout {AVChannelLayout value{};~ChannelLayout(){av_channel_layout_uninit(&value);}};
}
struct Reader::Impl{
    std::shared_ptr<resource::Source> source;
    std::function<bool()> cancelled;
    Track fixed_track{Track::any};
    std::uint64_t length{},position{};
    std::exception_ptr io_failure;
    ReaderStatistics stats;
    Information info;
    AVIOContext* io{};
    AVFormatContext* format{};
    AVPacket* packet{};
    std::deque<AVPacket*> queued;
    Codec video,audio;
    SwsContext* scaler{};
    SwrContext* resampler{};
    AVChannelLayout input_layout{};
    AVSampleFormat input_format{AV_SAMPLE_FMT_NONE};
    int input_rate{};
    bool resampler_flushed{},opening{};
    std::chrono::steady_clock::time_point open_deadline;
    Codec* receiving{};
    bool eof{};
    std::int64_t origin{},last_video{},last_audio{},video_duration{33333};
    ~Impl(){
        sws_freeContext(scaler);swr_free(&resampler);av_channel_layout_uninit(&input_layout);
        for(auto* value:queued)av_packet_free(&value);
        av_packet_free(&packet);avformat_close_input(&format);
        if(io){av_freep(&io->buffer);avio_context_free(&io);}
    }
    static int read(void* opaque,std::uint8_t* buffer,int count)noexcept{
        auto& self=*static_cast<Impl*>(opaque);
        try{
            if(count<0)return AVERROR(EINVAL);
            if(self.cancelled && self.cancelled())return AVERROR_EXIT;
            const auto bytes=std::min({std::uint64_t(count),self.length-self.position,std::uint64_t(32768)});
            if(!bytes)return AVERROR_EOF;
            self.source->read_at(self.position,{buffer,std::size_t(bytes)});self.position+=bytes;
            self.stats.bytes_read+=bytes;++self.stats.read_calls;
            self.stats.largest_read=std::max(self.stats.largest_read,std::size_t(bytes));
            return int(bytes);
        }catch(...){self.io_failure=std::current_exception();return AVERROR(EIO);}
    }
    static std::int64_t seek(void* opaque,std::int64_t offset,int whence)noexcept{
        auto& self=*static_cast<Impl*>(opaque);++self.stats.seek_calls;
        if(whence==AVSEEK_SIZE)return std::int64_t(self.length);
        whence&=~AVSEEK_FORCE;
        std::int64_t base{};
        if(whence==SEEK_CUR)base=std::int64_t(self.position);
        else if(whence==SEEK_END)base=std::int64_t(self.length);
        else if(whence!=SEEK_SET)return AVERROR(EINVAL);
        if((offset>0 && base>std::numeric_limits<std::int64_t>::max()-offset) ||
           (offset<0 && offset<-base))return AVERROR(EINVAL);
        const auto target=base+offset;
        if(target<0 || std::uint64_t(target)>self.length)return AVERROR(EINVAL);
        self.position=std::uint64_t(target);return target;
    }
    static int interrupt(void* opaque)noexcept{
        auto& self=*static_cast<Impl*>(opaque);
        try{return (self.opening && std::chrono::steady_clock::now()>=self.open_deadline) || (self.cancelled && self.cancelled());}
        catch(...){self.io_failure=std::current_exception();return 1;}
    }
    void checked(int result,const char* operation){
        if(io_failure)std::rethrow_exception(io_failure);
        if(result<0)fail(operation,result);
    }
    void open_codec(Codec& output,AVStream* stream,AVMediaType type){
        const auto codec=stream->codecpar->codec_id;
        const auto* decoder=avcodec_find_decoder(codec);
        if(!decoder || decoder->type!=type)throw Error(std::string("unsupported movie ")+(type==AVMEDIA_TYPE_VIDEO?"video":"audio")+" codec: "+avcodec_get_name(codec));
        output.stream=stream;
        if((fixed_track==Track::audio && type==AVMEDIA_TYPE_VIDEO) ||
           (fixed_track==Track::video && type==AVMEDIA_TYPE_AUDIO)){
            stream->discard=AVDISCARD_ALL;return;
        }
        output.context=avcodec_alloc_context3(decoder);
        if(!output.context)throw std::bad_alloc();
        checked(avcodec_parameters_to_context(output.context,stream->codecpar),"movie codec parameters");
        output.context->pkt_timebase=stream->time_base;
        // A low-end device must not create an unbounded decoder thread pool.
        output.context->thread_count=1;output.context->thread_type=0;
        checked(avcodec_open2(output.context,decoder,nullptr),"opening native movie decoder");
    }
    void open(){
        if(fixed_track!=Track::any && fixed_track!=Track::audio && fixed_track!=Track::video)throw Error("invalid fixed movie track");
        if(!source)throw Error("missing original movie source");length=source->size();
        if(!length || length>std::uint64_t(std::numeric_limits<std::int64_t>::max()))throw Error("invalid original movie length");
        auto* buffer=static_cast<std::uint8_t*>(av_malloc(32768));if(!buffer)throw std::bad_alloc();
        io=avio_alloc_context(buffer,32768,0,this,read,nullptr,seek);
        if(!io){av_free(buffer);throw std::bad_alloc();}io->seekable=AVIO_SEEKABLE_NORMAL;
        format=avformat_alloc_context();if(!format)throw std::bad_alloc();
        format->pb=io;format->flags|=AVFMT_FLAG_CUSTOM_IO;
        format->interrupt_callback={interrupt,this};
        format->probesize=512*1024;format->max_analyze_duration=1000000;format->max_probe_packets=128;
        opening=true;open_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        // Detect by content, including videos whose game filename ends in PAK.
        // ASF headers already declare their streams; preserve that fast path.
        checked(avformat_open_input(&format,nullptr,nullptr,nullptr),"opening movie container");
        bool incomplete=!format->nb_streams;
        for(unsigned i=0;i<format->nb_streams;++i){const auto* par=format->streams[i]->codecpar;
            if(par->codec_type==AVMEDIA_TYPE_VIDEO && (par->width<=0 || par->height<=0))incomplete=true;
            if(par->codec_type==AVMEDIA_TYPE_AUDIO && (par->sample_rate<=0 || par->ch_layout.nb_channels<=0))incomplete=true;
        }
        if(incomplete){
            std::vector<AVDictionary*> options(format->nb_streams,nullptr);
            for(auto& option:options)av_dict_set(&option,"threads","1",0);
            const auto status=avformat_find_stream_info(format,options.data());
            for(auto& option:options)av_dict_free(&option);
            checked(status,"reading movie stream information");
        }
        for(unsigned i=0;i<format->nb_streams;++i){auto* stream=format->streams[i];
            if(stream->codecpar->codec_type==AVMEDIA_TYPE_VIDEO && !(stream->disposition&AV_DISPOSITION_ATTACHED_PIC) && !video.stream)open_codec(video,stream,AVMEDIA_TYPE_VIDEO);
            else if(stream->codecpar->codec_type==AVMEDIA_TYPE_AUDIO && !audio.stream)open_codec(audio,stream,AVMEDIA_TYPE_AUDIO);
        }
        if(!video.stream)throw Error("movie has no supported video stream");
        const auto width=video.context?video.context->width:video.stream->codecpar->width;
        const auto height=video.context?video.context->height:video.stream->codecpar->height;
        if(width<1 || height<1 || width>8192 || height>8192)throw Error("invalid movie dimensions");
        info.width=unsigned(width);info.height=unsigned(height);
        if(audio.stream){
            const auto rate=audio.context?audio.context->sample_rate:audio.stream->codecpar->sample_rate;
            const auto channels=audio.context?audio.context->ch_layout.nb_channels:audio.stream->codecpar->ch_layout.nb_channels;
            if(rate<1000 || rate>384000 || channels<1 || channels>64)throw Error("unsupported movie audio format");
            info.audio_rate=unsigned(rate);info.audio_channels=unsigned(std::min(channels,2));
        }
        origin=format->start_time==AV_NOPTS_VALUE?0:format->start_time;
        if(format->duration!=AV_NOPTS_VALUE)info.duration_us=std::max<std::int64_t>(0,format->duration);
        if(!info.duration_us && video.stream->duration!=AV_NOPTS_VALUE)
            info.duration_us=av_rescale_q(video.stream->duration,video.stream->time_base,microseconds);
        const auto rate=video.stream->avg_frame_rate;
        if(rate.num>0 && rate.den>0)video_duration=av_rescale_q(1,av_inv_q(rate),microseconds);
        packet=av_packet_alloc();if(!packet)throw std::bad_alloc();
        opening=false;
    }
    std::int64_t timestamp(const AVFrame& frame,const Codec& codec,std::int64_t fallback)const{
        const auto value=frame.best_effort_timestamp==AV_NOPTS_VALUE?frame.pts:frame.best_effort_timestamp;
        return value==AV_NOPTS_VALUE?fallback:std::max<std::int64_t>(0,av_rescale_q(value,codec.stream->time_base,microseconds)-origin);
    }
    std::shared_ptr<AVFrame> normalise_video(std::shared_ptr<AVFrame> input){
        checked(av_frame_apply_cropping(input.get(),0),"cropping movie frame");
        if(input->width<1 || input->height<1 || input->width>8192 || input->height>8192)throw Error("invalid decoded movie dimensions");
        if((input->format==AV_PIX_FMT_YUV420P || input->format==AV_PIX_FMT_YUVJ420P) && input->linesize[0]>0 && input->linesize[1]>0 && input->linesize[2]>0)return input;
        auto format=AVPixelFormat(input->format);bool jpeg_range{};
        // JPEG aliases describe range, not a different plane layout. Normalise
        // the cache key so swscale does not recreate its context every frame.
        switch(format){
            case AV_PIX_FMT_YUVJ420P:format=AV_PIX_FMT_YUV420P;jpeg_range=true;break;
            case AV_PIX_FMT_YUVJ422P:format=AV_PIX_FMT_YUV422P;jpeg_range=true;break;
            case AV_PIX_FMT_YUVJ444P:format=AV_PIX_FMT_YUV444P;jpeg_range=true;break;
            case AV_PIX_FMT_YUVJ440P:format=AV_PIX_FMT_YUV440P;jpeg_range=true;break;
            case AV_PIX_FMT_YUVJ411P:format=AV_PIX_FMT_YUV411P;jpeg_range=true;break;
            default:break;
        }
        const auto* description=av_pix_fmt_desc_get(format);
        if(!description || !sws_isSupportedInput(format))throw Error("unsupported movie pixel format");
        auto* frame=av_frame_alloc();if(!frame)throw std::bad_alloc();std::shared_ptr<AVFrame> output(frame,[](AVFrame* value){av_frame_free(&value);});
        frame->format=AV_PIX_FMT_YUV420P;frame->width=input->width;frame->height=input->height;
        checked(av_frame_get_buffer(frame,32),"allocating converted movie frame");checked(av_frame_copy_props(frame,input.get()),"copying movie frame properties");
        scaler=sws_getCachedContext(scaler,input->width,input->height,format,input->width,input->height,AV_PIX_FMT_YUV420P,SWS_BILINEAR,nullptr,nullptr,nullptr);
        if(!scaler)throw Error("cannot create movie pixel converter");
        const bool full=jpeg_range || input->color_range==AVCOL_RANGE_JPEG || (description->flags&AV_PIX_FMT_FLAG_RGB);
        const bool hd=input->colorspace==AVCOL_SPC_BT709 || input->colorspace==AVCOL_SPC_BT2020_NCL || input->colorspace==AVCOL_SPC_BT2020_CL;
        const int source_matrix=(input->colorspace==AVCOL_SPC_BT2020_NCL || input->colorspace==AVCOL_SPC_BT2020_CL)?SWS_CS_BT2020:hd?SWS_CS_ITU709:SWS_CS_ITU601;
        checked(sws_setColorspaceDetails(scaler,sws_getCoefficients(source_matrix),full,sws_getCoefficients(hd?SWS_CS_ITU709:SWS_CS_ITU601),full,0,1<<16,1<<16),"setting movie colour conversion");
        if(sws_scale(scaler,input->data,input->linesize,0,input->height,frame->data,frame->linesize)!=frame->height)throw Error("incomplete movie pixel conversion");
        frame->color_range=full?AVCOL_RANGE_JPEG:AVCOL_RANGE_MPEG;frame->colorspace=hd?AVCOL_SPC_BT709:AVCOL_SPC_SMPTE170M;
        return output;
    }
    void little_endian(AudioFrame& output){
        if constexpr(std::endian::native==std::endian::big)for(std::size_t n=0;n<output.pcm16.size();n+=2)std::swap(output.pcm16[n],output.pcm16[n+1]);
    }
    AudioFrame resample(const AVFrame& frame,const Codec& codec){
        ChannelLayout layout;
        if(frame.ch_layout.order==AV_CHANNEL_ORDER_UNSPEC || !av_channel_layout_check(&frame.ch_layout))av_channel_layout_default(&layout.value,frame.ch_layout.nb_channels);
        else checked(av_channel_layout_copy(&layout.value,&frame.ch_layout),"reading movie audio layout");
        if(!resampler || input_format!=frame.format || input_rate!=frame.sample_rate || av_channel_layout_compare(&input_layout,&layout.value)){
            swr_free(&resampler);av_channel_layout_uninit(&input_layout);checked(av_channel_layout_copy(&input_layout,&layout.value),"saving movie audio layout");
            ChannelLayout output;av_channel_layout_default(&output.value,int(info.audio_channels));
            checked(swr_alloc_set_opts2(&resampler,&output.value,AV_SAMPLE_FMT_S16,int(info.audio_rate),&layout.value,AVSampleFormat(frame.format),frame.sample_rate,0,nullptr),"creating movie audio converter");
            checked(swr_init(resampler),"initialising movie audio converter");input_format=AVSampleFormat(frame.format);input_rate=frame.sample_rate;resampler_flushed=false;
        }
        const auto delay=swr_get_delay(resampler,frame.sample_rate);
        const auto capacity=av_rescale_rnd(delay+frame.nb_samples,info.audio_rate,frame.sample_rate,AV_ROUND_UP);
        if(capacity<0 || capacity>262144)throw Error("movie audio conversion exceeds its buffer limit");
        AudioFrame result;result.rate=info.audio_rate;result.channels=info.audio_channels;
        result.time_us=std::max(last_audio,std::max<std::int64_t>(0,timestamp(frame,codec,last_audio)-delay*1000000/frame.sample_rate));
        result.pcm16.resize(std::size_t(capacity)*result.channels*2);auto* output=result.pcm16.data();
        std::array<const std::uint8_t*,64> inputs{};const auto planes=av_sample_fmt_is_planar(AVSampleFormat(frame.format))?frame.ch_layout.nb_channels:1;
        if(!frame.extended_data)throw Error("missing decoded movie audio data");
        for(int n=0;n<planes;++n){if(!frame.extended_data[n])throw Error("missing decoded movie audio plane");inputs[n]=frame.extended_data[n];}
        const auto count=swr_convert(resampler,&output,int(capacity),inputs.data(),frame.nb_samples);checked(count,"converting movie audio");
        result.pcm16.resize(std::size_t(count)*result.channels*2);last_audio=result.time_us+std::int64_t(count)*1000000/result.rate;little_endian(result);return result;
    }
    std::optional<Frame> drain_resampler(){
        if(!resampler || resampler_flushed)return {};
        AudioFrame result;result.rate=info.audio_rate;result.channels=info.audio_channels;result.time_us=last_audio;
        result.pcm16.resize(65536*result.channels*2);auto* output=result.pcm16.data();
        const auto count=swr_convert(resampler,&output,65536,nullptr,0);checked(count,"draining movie audio converter");
        if(!count){resampler_flushed=true;return {};}
        result.pcm16.resize(std::size_t(count)*result.channels*2);last_audio+=std::int64_t(count)*1000000/result.rate;little_endian(result);++stats.audio_frames;return result;
    }
    Frame convert(AVFrame* frame,Codec& codec){
        std::shared_ptr<AVFrame> owned(frame,[](AVFrame* value){av_frame_free(&value);});
        if(&codec==&video){
            owned=normalise_video(std::move(owned));frame=owned.get();
            VideoFrame result;result.owner=owned;result.width=frame->width;result.height=frame->height;
            result.time_us=timestamp(*frame,codec,last_video);
            result.duration_us=frame->duration>0?av_rescale_q(frame->duration,codec.stream->time_base,microseconds):video_duration;
            last_video=result.time_us+result.duration_us;
            if(frame->sample_aspect_ratio.num>0 && frame->sample_aspect_ratio.den>0){
                result.aspect_numerator=frame->sample_aspect_ratio.num;result.aspect_denominator=frame->sample_aspect_ratio.den;
            }
            result.matrix=frame->colorspace==AVCOL_SPC_BT709?ColourMatrix::bt709:ColourMatrix::bt601;
            result.full_range=frame->color_range==AVCOL_RANGE_JPEG || frame->format==AV_PIX_FMT_YUVJ420P;
            for(unsigned plane=0;plane<3;++plane){const auto width=plane?(result.width+1)/2:result.width,height=plane?(result.height+1)/2:result.height;
                if(!frame->data[plane] || frame->linesize[plane]<width)throw Error("invalid native movie frame plane");
                result.planes[plane]={frame->data[plane],frame->linesize[plane],width,height};}
            ++stats.video_frames;return result;
        }
        if(frame->sample_rate<1000 || frame->sample_rate>384000 || frame->ch_layout.nb_channels<1 || frame->ch_layout.nb_channels>64 || frame->nb_samples<0 || frame->nb_samples>65536)
            throw Error("unsupported decoded movie audio format");
        if(resampler || frame->format!=AV_SAMPLE_FMT_FLTP || frame->sample_rate!=int(info.audio_rate) || frame->ch_layout.nb_channels!=int(info.audio_channels)){
            auto result=resample(*frame,codec);++stats.audio_frames;return result;
        }
        AudioFrame result;result.rate=info.audio_rate;result.channels=info.audio_channels;
        result.time_us=timestamp(*frame,codec,last_audio);last_audio=result.time_us+std::int64_t(frame->nb_samples)*1000000/result.rate;
        result.pcm16.resize(std::size_t(frame->nb_samples)*result.channels*2);
        for(unsigned channel=0;channel<result.channels;++channel){
            if(!frame->extended_data || !frame->extended_data[channel])throw Error("missing decoded movie audio plane");
            const auto* samples=reinterpret_cast<const float*>(frame->extended_data[channel]);
            for(int n=0;n<frame->nb_samples;++n){const auto sample=samples[n];
                if(!std::isfinite(sample))throw Error("nonfinite decoded movie audio");
                const auto value=std::int16_t(std::clamp(sample*32768.f,-32768.f,32767.f));
                const auto at=(std::size_t(n)*result.channels+channel)*2;
                result.pcm16[at]=std::uint8_t(value);result.pcm16[at+1]=std::uint8_t(std::uint16_t(value)>>8);
            }
        }
        ++stats.audio_frames;return result;
    }
    Codec* packet_codec(const AVPacket& value){
        return value.stream_index==video.stream->index?&video:
            (audio.stream && value.stream_index==audio.stream->index?&audio:nullptr);
    }
    void submit(Codec& codec,AVPacket* value){
        checked(avcodec_send_packet(codec.context,value),value?"submitting original movie packet":"flushing native movie decoder");
        codec.draining=true;receiving=&codec;if(!value)codec.flushed=true;
    }
    std::optional<Frame> next(Track preference){
        if(fixed_track!=Track::any){
            if(preference!=Track::any && preference!=fixed_track)throw Error("movie reader requested a track excluded at construction");
            preference=fixed_track;
        }
        Codec* preferred=preference==Track::video?&video:(preference==Track::audio?&audio:nullptr);
        if(preferred && !preferred->context)return {};
        for(;;){
            if(interrupt(this)){if(io_failure)std::rethrow_exception(io_failure);throw Error("native movie decoding cancelled");}
            Codec* active=preferred;
            if(!active){
                if(receiving && receiving->draining)active=receiving;
                else if(video.draining)active=&video;
                else if(audio.draining)active=&audio;
            }
            if(active && active->draining){
                auto* frame=av_frame_alloc();if(!frame)throw std::bad_alloc();
                const auto status=avcodec_receive_frame(active->context,frame);
                if(status==0){if(active==&audio && !frame->nb_samples){av_frame_free(&frame);continue;}
                    auto result=convert(frame,*active);if(const auto* sound=std::get_if<AudioFrame>(&result);sound && sound->pcm16.empty())continue;return result;}
                av_frame_free(&frame);
                if(status!=AVERROR(EAGAIN) && status!=AVERROR_EOF)checked(status,"decoding original movie frame");
                active->draining=false;if(receiving==active)receiving=nullptr;continue;
            }
            const auto pending=std::find_if(queued.begin(),queued.end(),[&](const auto* value){return !preferred || packet_codec(*value)==preferred;});
            if(pending!=queued.end()){
                auto* value=*pending;queued.erase(pending);stats.queued_packet_bytes-=std::size_t(value->size);
                auto* target=packet_codec(*value);const auto sent=avcodec_send_packet(target->context,value);av_packet_free(&value);
                checked(sent,"submitting buffered original movie packet");target->draining=true;receiving=target;continue;
            }
            if(eof){
                if(preferred){if(preferred->flushed)return preferred==&audio?drain_resampler():std::optional<Frame>{};submit(*preferred,nullptr);continue;}
                Codec* unflushed{};
                for(auto* codec:{&video,&audio})if(codec->context && !codec->flushed){unflushed=codec;break;}
                if(!unflushed)return drain_resampler();
                submit(*unflushed,nullptr);continue;
            }
            const auto status=av_read_frame(format,packet);
            if(status==AVERROR_EOF){if(io_failure)std::rethrow_exception(io_failure);eof=true;continue;}
            checked(status,"reading original movie packet");++stats.packets;
            auto* target=packet_codec(*packet);
            if(!target || !target->context){av_packet_unref(packet);continue;}
            if(packet->size<0 || packet->size>8*1024*1024){av_packet_unref(packet);throw Error("original movie packet exceeds the buffer limit");}
            if(!preferred || target==preferred){
                const auto sent=avcodec_send_packet(target->context,packet);av_packet_unref(packet);
                checked(sent,"submitting original movie packet");target->draining=true;receiving=target;
            }else{
                if(queued.size()>=2048 || std::size_t(packet->size)>8*1024*1024-stats.queued_packet_bytes){
                    av_packet_unref(packet);throw Error("original movie interleave exceeds the compressed packet limit");
                }
                auto* retained=av_packet_clone(packet);av_packet_unref(packet);if(!retained)throw std::bad_alloc();
                try{queued.push_back(retained);}catch(...){av_packet_free(&retained);throw;}
                stats.queued_packet_bytes+=std::size_t(retained->size);
                stats.peak_packet_bytes=std::max(stats.peak_packet_bytes,stats.queued_packet_bytes);
                stats.peak_packet_count=std::max(stats.peak_packet_count,queued.size());
            }
        }
    }
};
Reader::Reader(std::shared_ptr<resource::Source> source,std::function<bool()> cancelled,Track fixed_track):impl_(std::make_unique<Impl>()){
    impl_->source=std::move(source);impl_->cancelled=std::move(cancelled);impl_->fixed_track=fixed_track;impl_->open();
}
Reader::~Reader()=default;
const Information& Reader::information()const noexcept{return impl_->info;}
ReaderStatistics Reader::statistics()const noexcept{return impl_->stats;}
std::optional<Frame> Reader::next(Track preference){return impl_->next(preference);}
}
