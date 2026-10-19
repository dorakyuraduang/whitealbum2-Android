#include "movie_reader.hpp"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
extern "C"{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
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
    Codec* receiving{};
    bool eof{};
    std::int64_t origin{},last_video{},last_audio{},video_duration{33333};
    ~Impl(){
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
        try{return self.cancelled && self.cancelled();}
        catch(...){self.io_failure=std::current_exception();return 1;}
    }
    void checked(int result,const char* operation){
        if(io_failure)std::rethrow_exception(io_failure);
        if(result<0)fail(operation,result);
    }
    void open_codec(Codec& output,AVStream* stream,AVCodecID expected){
        if(stream->codecpar->codec_id!=expected)throw Error("unsupported original movie codec");
        output.stream=stream;
        if((fixed_track==Track::audio && expected==AV_CODEC_ID_WMV3) ||
           (fixed_track==Track::video && expected==AV_CODEC_ID_WMAV2)){
            stream->discard=AVDISCARD_ALL;return;
        }
        const auto* decoder=avcodec_find_decoder(expected);
        if(!decoder)throw Error("required native movie decoder was not built");
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
        const auto* demuxer=av_find_input_format("asf");if(!demuxer)throw Error("native ASF demuxer was not built");
        checked(avformat_open_input(&format,nullptr,demuxer,nullptr),"opening original ASF movie");
        // ASF declares stream parameters in its header; avoid scanning seconds
        // of the file with avformat_find_stream_info before the first frame.
        for(unsigned i=0;i<format->nb_streams;++i){auto* stream=format->streams[i];
            if(stream->codecpar->codec_type==AVMEDIA_TYPE_VIDEO && !video.stream)open_codec(video,stream,AV_CODEC_ID_WMV3);
            else if(stream->codecpar->codec_type==AVMEDIA_TYPE_AUDIO && !audio.stream)open_codec(audio,stream,AV_CODEC_ID_WMAV2);
        }
        if(!video.stream)throw Error("original movie has no WMV3 video stream");
        const auto width=video.context?video.context->width:video.stream->codecpar->width;
        const auto height=video.context?video.context->height:video.stream->codecpar->height;
        if(width<1 || height<1 || width>8192 || height>8192)throw Error("invalid movie dimensions");
        info.width=unsigned(width);info.height=unsigned(height);
        if(audio.stream){
            const auto rate=audio.context?audio.context->sample_rate:audio.stream->codecpar->sample_rate;
            const auto channels=audio.context?audio.context->ch_layout.nb_channels:audio.stream->codecpar->ch_layout.nb_channels;
            if(rate<1000 || rate>384000 || channels<1 || channels>2)throw Error("unsupported movie audio format");
            info.audio_rate=unsigned(rate);info.audio_channels=unsigned(channels);
        }
        origin=format->start_time==AV_NOPTS_VALUE?0:format->start_time;
        if(format->duration!=AV_NOPTS_VALUE)info.duration_us=std::max<std::int64_t>(0,format->duration);
        if(!info.duration_us && video.stream->duration!=AV_NOPTS_VALUE)
            info.duration_us=av_rescale_q(video.stream->duration,video.stream->time_base,microseconds);
        const auto rate=video.stream->avg_frame_rate;
        if(rate.num>0 && rate.den>0)video_duration=av_rescale_q(1,av_inv_q(rate),microseconds);
        packet=av_packet_alloc();if(!packet)throw std::bad_alloc();
    }
    std::int64_t timestamp(const AVFrame& frame,const Codec& codec,std::int64_t fallback)const{
        const auto value=frame.best_effort_timestamp==AV_NOPTS_VALUE?frame.pts:frame.best_effort_timestamp;
        return value==AV_NOPTS_VALUE?fallback:std::max<std::int64_t>(0,av_rescale_q(value,codec.stream->time_base,microseconds)-origin);
    }
    Frame convert(AVFrame* frame,Codec& codec){
        std::shared_ptr<AVFrame> owned(frame,[](AVFrame* value){av_frame_free(&value);});
        if(&codec==&video){
            if(frame->format!=AV_PIX_FMT_YUV420P)throw Error("unsupported native movie pixel format");
            checked(av_frame_apply_cropping(frame,0),"cropping native movie frame");
            if(frame->width<1 || frame->height<1 || frame->width>8192 || frame->height>8192)throw Error("invalid decoded movie dimensions");
            VideoFrame result;result.owner=owned;result.width=frame->width;result.height=frame->height;
            result.time_us=timestamp(*frame,codec,last_video);
            result.duration_us=frame->duration>0?av_rescale_q(frame->duration,codec.stream->time_base,microseconds):video_duration;
            last_video=result.time_us+result.duration_us;
            if(frame->sample_aspect_ratio.num>0 && frame->sample_aspect_ratio.den>0){
                result.aspect_numerator=frame->sample_aspect_ratio.num;result.aspect_denominator=frame->sample_aspect_ratio.den;
            }
            result.matrix=frame->colorspace==AVCOL_SPC_BT709?ColourMatrix::bt709:ColourMatrix::bt601;
            result.full_range=frame->color_range==AVCOL_RANGE_JPEG;
            for(unsigned plane=0;plane<3;++plane){const auto width=plane?(result.width+1)/2:result.width,height=plane?(result.height+1)/2:result.height;
                if(!frame->data[plane] || frame->linesize[plane]<width)throw Error("invalid native movie frame plane");
                result.planes[plane]={frame->data[plane],frame->linesize[plane],width,height};}
            ++stats.video_frames;return result;
        }
        if(frame->format!=AV_SAMPLE_FMT_FLTP || frame->sample_rate!=int(info.audio_rate) ||
           frame->ch_layout.nb_channels!=int(info.audio_channels) || frame->nb_samples<0 || frame->nb_samples>65536)
            throw Error("unsupported decoded movie audio format");
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
                if(status==0)return convert(frame,*active);
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
                if(preferred){if(preferred->flushed)return {};submit(*preferred,nullptr);continue;}
                Codec* unflushed{};
                for(auto* codec:{&video,&audio})if(codec->context && !codec->flushed){unflushed=codec;break;}
                if(!unflushed)return {};
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
