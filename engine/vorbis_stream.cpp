// Native streaming codec. It has no dependency on the legacy CPU or an OS API.
#include "vorbis_stream.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PULLDATA_API
#define STB_VORBIS_NO_INTEGER_CONVERSION
#include "../third_party/stb/stb_vorbis.c"
namespace wa2::audio {
VorbisFormat query_vorbis_format(std::span<const std::uint8_t> page){
    // The supported push decoder requires one 30-byte identification packet
    // on the first Ogg page. Match those constraints, including the Ogg CRC,
    // without parsing the substantially larger comment/setup packets twice.
    constexpr std::size_t PageBytes=27+1+30;
    if(page.size()<PageBytes)throw VorbisError("Incomplete Vorbis identification page");
    auto le32=[&](std::size_t at){return std::uint32_t(page[at])|(std::uint32_t(page[at+1])<<8)|
        (std::uint32_t(page[at+2])<<16)|(std::uint32_t(page[at+3])<<24);};
    if(!std::equal(page.begin(),page.begin()+4,"OggS") || page[4] || page[5]!=2 || page[26]!=1 || page[27]!=30 ||
       page[28]!=1 || !std::equal(page.begin()+29,page.begin()+35,"vorbis") || le32(35))
        throw VorbisError("Invalid Vorbis identification page");
    std::uint32_t crc=0;
    for(std::size_t at=0;at<PageBytes;++at){
        crc^=std::uint32_t(at>=22 && at<26?0:page[at])<<24;
        for(unsigned bit=0;bit<8;++bit)crc=(crc<<1)^((crc&0x80000000u)?0x04c11db7u:0u);
    }
    if(crc!=le32(22))throw VorbisError("Vorbis identification CRC mismatch");
    const unsigned channels=page[39],rate=le32(40),small=page[56]&15,large=page[56]>>4;
    if(channels<1 || channels>8 || rate<1000 || rate>384000 || small<6 || large>13 || small>large || !(page[57]&1))
        throw VorbisError("Unsupported Vorbis identification format");
    return {channels,rate};
}
struct VorbisStream::Impl {
    stb_vorbis* decoder{};
    std::vector<std::uint8_t> input,pcm;std::size_t pcm_offset{};
    unsigned channels{},rate{};bool ended{},end_requested{};
    ~Impl(){if(decoder)stb_vorbis_close(decoder);}
};
VorbisStream::VorbisStream():impl_(std::make_unique<Impl>()){}
VorbisStream::~VorbisStream()=default;
std::size_t VorbisStream::buffered_bytes() const noexcept {return impl_->input.size()+impl_->pcm.size()-impl_->pcm_offset;}
VorbisStream::Result VorbisStream::push(std::span<const std::uint8_t> input,std::span<std::uint8_t> output,bool end_of_input){
    constexpr std::size_t MaximumBuffered=4*1024*1024;
    auto& s=*impl_;
    if(s.ended && !input.empty())throw VorbisError("Vorbis input after end of stream");
    const auto accepted=std::min(input.size(),MaximumBuffered-s.input.size());
    s.input.insert(s.input.end(),input.begin(),input.begin()+accepted);
    if(end_of_input && accepted==input.size())s.end_requested=true;
    std::size_t consumed{},written{};bool needs_data{};
    auto drain=[&]{const auto n=std::min(output.size()-written,s.pcm.size()-s.pcm_offset);if(n)std::memcpy(output.data()+written,s.pcm.data()+s.pcm_offset,n);written+=n;s.pcm_offset+=n;if(s.pcm_offset==s.pcm.size()){s.pcm.clear();s.pcm_offset=0;}};
    drain();
    if(!s.decoder){
        int used=0,error=0;
        s.decoder=stb_vorbis_open_pushdata(s.input.data(),int(s.input.size()),&used,&error,nullptr);
        if(!s.decoder){if(error!=VORBIS_need_more_data || s.end_requested || s.input.size()==MaximumBuffered)throw VorbisError("Vorbis header error "+std::to_string(error));return {written,false,0,0,accepted};}
        consumed=std::size_t(used);const auto info=stb_vorbis_get_info(s.decoder);
        if(info.channels<1 || info.channels>8 || info.sample_rate<1000 || info.sample_rate>384000)throw VorbisError("Unsupported Vorbis format");
        s.channels=unsigned(info.channels);s.rate=info.sample_rate;
    }
    while(written<output.size() && s.pcm.empty() && consumed<s.input.size()){
        float** samples=nullptr;int channels=0,count=0;
        const auto used=stb_vorbis_decode_frame_pushdata(s.decoder,s.input.data()+consumed,int(s.input.size()-consumed),&channels,&samples,&count);
        if(used<0 || std::size_t(used)>s.input.size()-consumed || count<0)throw VorbisError("Invalid Vorbis decoder progress");
        const auto error=stb_vorbis_get_error(s.decoder);
        if(error && error!=VORBIS_need_more_data)throw VorbisError("Vorbis packet error "+std::to_string(error));
        consumed+=std::size_t(used);
        if(count){
            if(channels!=int(s.channels) || !samples || std::size_t(count)>MaximumBuffered/(s.channels*2))throw VorbisError("Invalid Vorbis output format");
            s.pcm.resize(std::size_t(count)*s.channels*2);
            for(int frame=0;frame<count;++frame)for(unsigned channel=0;channel<s.channels;++channel){
                const double scaled=double(samples[channel][frame])*32767.0;
                const auto value=std::isfinite(scaled)?std::int32_t(std::clamp(scaled,-32768.0,32767.0)):0;
                const auto at=(std::size_t(frame)*s.channels+channel)*2;
                s.pcm[at]=std::uint8_t(value);s.pcm[at+1]=std::uint8_t(std::uint32_t(value)>>8);
            }
            drain();
        }
        if(!used && !count){needs_data=true;break;}
    }
    if(consumed)s.input.erase(s.input.begin(),s.input.begin()+consumed);
    if(s.end_requested && s.pcm.empty() && (s.input.empty() || needs_data)){
        if(s.input.size()>3)throw VorbisError("Incomplete Vorbis packet at end of stream");
        s.input.clear();s.ended=true;
    }
    return {written,s.ended,s.channels,s.rate,accepted};
}
}
