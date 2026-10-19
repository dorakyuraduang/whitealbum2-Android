#include "ogg_reader.hpp"
#include <algorithm>

namespace wa2::audio {
OggReader::OggReader(std::shared_ptr<resource::Source> source):source_(std::move(source)){
    if(!source_ || source_->size()<58)throw VorbisError("truncated Ogg resource");
    std::array<std::uint8_t,58> header{};source_->read_at(0,header);format_=query_vorbis_format(header);
    if(format_.channels>2)throw VorbisError("game audio must be mono or stereo");
    rewind();
}
void OggReader::rewind(){decoder_=std::make_unique<VorbisStream>();begin_=end_=0;offset_=0;finished_=false;need_input_=true;}
std::size_t OggReader::buffered_bytes()const noexcept{return decoder_->buffered_bytes()+end_-begin_;}
std::size_t OggReader::read(std::span<std::uint8_t> pcm){
    if(pcm.size()%(format_.channels*2))throw VorbisError("unaligned Ogg PCM output");
    if(pcm.empty() || finished_)return 0;
    std::size_t written{};
    while(written<pcm.size()){
        if(need_input_ && begin_==end_ && offset_<source_->size()){
            begin_=0;end_=std::size_t(std::min<std::uint64_t>(input_.size(),source_->size()-offset_));
            source_->read_at(offset_,std::span(input_).first(end_));offset_+=end_;
        }
        const auto result=decoder_->push(std::span(input_).subspan(begin_,end_-begin_),pcm.subspan(written),offset_==source_->size());
        begin_+=result.accepted;written+=result.bytes;
        need_input_=false;
        if(result.channels && VorbisFormat{result.channels,result.rate}!=format_)
            throw VorbisError("Ogg resource changed PCM format");
        if(result.finished){finished_=true;break;}
        if(!result.accepted && !result.bytes){
            if(begin_==end_ && offset_<source_->size()){need_input_=true;continue;}
            throw VorbisError("Ogg decoder made no progress");
        }
    }
    return written;
}
}
