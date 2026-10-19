#include "wave_reader.hpp"
#include <algorithm>
#include <array>
#include <cstring>

namespace wa2::audio{
namespace{
unsigned word(const std::uint8_t* data){return unsigned(data[0])|(unsigned(data[1])<<8);}
std::uint32_t dword(const std::uint8_t* data){return word(data)|(std::uint32_t(word(data+2))<<16);}
}
WaveReader::WaveReader(std::shared_ptr<resource::Source> source):source_(std::move(source)){
    if(!source_ || source_->size()<12)throw resource::Error("truncated WAV resource");
    std::array<std::uint8_t,12> header{};source_->read_at(0,header);
    if(std::memcmp(header.data(),"RIFF",4) || std::memcmp(header.data()+8,"WAVE",4))throw resource::Error("unsupported WAV header");
    const auto end=std::uint64_t(dword(header.data()+4))+8;
    if(end<12 || end>source_->size())throw resource::Error("WAV RIFF length exceeds source");
    bool have_format=false,have_data=false;unsigned chunks=0;
    for(std::uint64_t at=12;at<end;){
        if(++chunks>1024 || end-at<8)throw resource::Error("invalid WAV chunk directory");
        std::array<std::uint8_t,8> chunk{};source_->read_at(at,chunk);const auto size=dword(chunk.data()+4);at+=8;
        if(size>end-at)throw resource::Error("WAV chunk exceeds RIFF boundary");
        if(!std::memcmp(chunk.data(),"fmt ",4)){
            if(have_format || size<16 || size>64)throw resource::Error("invalid WAV format chunk");
            std::array<std::uint8_t,64> data{};source_->read_at(at,std::span(data).first(size));
            auto tag=word(data.data());const auto bits=word(data.data()+14);format_={word(data.data()+2),dword(data.data()+4)};
            if(tag==0xfffe){
                constexpr std::array<std::uint8_t,16> pcm_guid{1,0,0,0,0,0,16,0,128,0,0,170,0,56,155,113};
                if(size<40 || word(data.data()+16)<22 || word(data.data()+18)!=bits ||
                   !std::equal(pcm_guid.begin(),pcm_guid.end(),data.begin()+24))throw resource::Error("unsupported extensible WAV format");
                tag=1;
            }
            if(tag!=1 || (bits!=8 && bits!=16))throw resource::Error("WAV codec or bit depth is not implemented");
            if(format_.channels<1 || format_.channels>2 || format_.rate<1000 || format_.rate>384000)
                throw resource::Error("unsupported WAV channel count or rate");
            sample_bytes_=bits/8;const auto block=format_.channels*sample_bytes_;
            if(word(data.data()+12)!=block || dword(data.data()+8)!=format_.rate*block)throw resource::Error("inconsistent WAV PCM format");
            have_format=true;
        }else if(!std::memcmp(chunk.data(),"data",4)){
            if(have_data)throw resource::Error("multiple WAV data chunks are not implemented");data_=at;bytes_=size;have_data=true;
        }
        at+=std::uint64_t(size)+(size&1);
        if(at>end)throw resource::Error("WAV chunk padding exceeds RIFF boundary");
    }
    if(!have_format || !have_data || bytes_%(format_.channels*sample_bytes_))throw resource::Error("missing or unaligned WAV PCM data");
}
std::size_t WaveReader::read(std::span<std::uint8_t> output){
    const auto output_frame=format_.channels*2,source_frame=format_.channels*sample_bytes_;
    if(output.size()%output_frame)throw resource::Error("unaligned WAV PCM output");
    std::array<std::uint8_t,8192> input{};std::size_t written=0;
    while(written<output.size() && cursor_<bytes_){
        const auto count=std::min({(bytes_-cursor_)/source_frame,std::uint64_t((output.size()-written)/output_frame),std::uint64_t(input.size()/source_frame)});
        if(sample_bytes_==2)source_->read_at(data_+cursor_,output.subspan(written,std::size_t(count)*source_frame));
        else{
            source_->read_at(data_+cursor_,std::span(input).first(std::size_t(count)*source_frame));
            for(std::size_t n=0;n<std::size_t(count)*format_.channels;++n){const auto value=(int(input[n])-128)*256;
                output[written+n*2]=std::uint8_t(value);output[written+n*2+1]=std::uint8_t(std::uint16_t(value)>>8);}
        }
        cursor_+=count*source_frame;written+=std::size_t(count)*output_frame;
    }
    return written;
}
}
