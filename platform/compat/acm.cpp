#include "acm.hpp"
#include "../../engine/wave_codec.hpp"
#include "../../port/guest_api.hpp"
#include <algorithm>
#include <map>
namespace wa2::platform::compat {namespace {
using namespace wr;using namespace wr::port;
constexpr U32 invalid_handle=5,invalid_parameter=11,not_possible=512,unprepared=514;
audio::WaveCodecFormat format(Process& p,U32 at){const auto tag=p.memory.load(at,16);const auto extra=tag==1?0:p.memory.load(at+16,16);if(extra>4096)throw std::invalid_argument("ACM format extension limit");std::vector<std::uint8_t> bytes(tag==1?16:18+extra);p.memory.copy_out(at,bytes);return audio::WaveCodecFormat::parse(bytes);}
struct Header {U32 source,destination,source_capacity,destination_capacity;};
struct Stream {
    audio::WaveCodecFormat source,destination;audio::WaveBlockDecoder decoder;
    std::map<U32,Header> headers;std::vector<std::uint8_t> pending;std::size_t cursor{};
    Stream(audio::WaveCodecFormat s,audio::WaveCodecFormat d):source(std::move(s)),destination(std::move(d)),decoder(source){}
};
class Acm {
    Process& p;std::map<U32,std::unique_ptr<Stream>> streams;std::uint64_t opened{},conversions{},decoded{},written{};
    Stream* stream(U32 id){const auto found=streams.find(id);return found==streams.end()?nullptr:found->second.get();}
    U32 prepare(Args a){auto* s=stream(a[0]);if(!s)return invalid_handle;if(a[2])return invalid_parameter;p.memory.check(a[1],84,Memory::Read|Memory::Write);const auto fields=read<std::array<U32,21>>(p,a[1]);
        if(fields[0]<84 || fields[4]>16u*1024*1024 || fields[8]>16u*1024*1024)return invalid_parameter;
        if(!s->headers.contains(a[1]) && s->headers.size()>=64)return U32(4);
        p.memory.check(fields[3],fields[4],Memory::Read);p.memory.check(fields[7],fields[8],Memory::Write);
        s->headers[a[1]]={fields[3],fields[7],fields[4],fields[8]};p.memory.store(a[1]+4,(fields[1]|0x20000u)&~0x100000u,32);return U32(0);
    }
    U32 convert(Args a){auto* s=stream(a[0]);if(!s)return invalid_handle;const auto found=s->headers.find(a[1]);if(found==s->headers.end())return unprepared;
        if(a[2]&~0x34u)return invalid_parameter;p.memory.check(a[1],84,Memory::Read|Memory::Write);const auto fields=read<std::array<U32,21>>(p,a[1]);const auto& header=found->second;
        if(fields[0]<84 || fields[3]!=header.source || fields[7]!=header.destination || fields[4]>header.source_capacity || fields[8]>header.destination_capacity)return invalid_parameter;
        p.memory.check(header.source,fields[4],Memory::Read);p.memory.check(header.destination,fields[8],Memory::Write);
        if(a[2]&0x10){s->decoder.reset();s->pending.clear();s->cursor=0;}
        const auto capacity=fields[8]/s->destination.block*s->destination.block;U32 input{},output{};
        while(output<capacity){
            if(s->cursor<s->pending.size()){const auto count=std::min<std::size_t>(s->pending.size()-s->cursor,capacity-output);p.memory.copy_in(header.destination+output,std::span(s->pending).subspan(s->cursor,count));s->cursor+=count;output+=U32(count);continue;}
            s->pending.clear();s->cursor=0;if(fields[4]-input<s->source.block)break;
            // Only one compressed block is staged. Prepared headers retain
            // source addresses/capacities, never another pair of large buffers.
            const auto bytes=s->source.tag==2 || s->source.tag==17?s->source.block:std::min<U32>((fields[4]-input)/s->source.block*s->source.block,65536/s->source.block*s->source.block);
            std::vector<std::uint8_t> block(bytes);p.memory.copy_out(header.source+input,block);s->pending=s->decoder.decode(block);input+=bytes;decoded+=s->pending.size();
            if(s->pending.empty())throw std::runtime_error("WAVE block decoder produced no PCM");
        }
        p.memory.store(a[1]+4,(fields[1]|0x30000u)&~0x100000u,32);p.memory.store(a[1]+20,input,32);p.memory.store(a[1]+36,output,32);++conversions;written+=output;return U32(0);
    }
public:
    explicit Acm(Process& value):p(value){}
    void shutdown(){streams.clear();}
    std::string report()const{std::size_t pending{},headers{};for(const auto& [id,s]:streams){pending+=s->pending.size()-s->cursor;headers+=s->headers.size();}return "{\"backend\":\"shared-native-wave-codecs\",\"streams_opened\":"+std::to_string(opened)+",\"live_streams\":"+std::to_string(streams.size())+",\"prepared_headers\":"+std::to_string(headers)+",\"pending_pcm_bytes\":"+std::to_string(pending)+",\"conversions\":"+std::to_string(conversions)+",\"decoded_bytes\":"+std::to_string(decoded)+",\"written_bytes\":"+std::to_string(written)+"}";}
    void install(){
        api(p,"msacm32.dll","acmFormatSuggest",5,[this](Process&,Args a){if(a[0])return invalid_handle;if(a[3]<18 || a[3]>4114 || a[4]&~0xf0000u)return invalid_parameter;
            const auto source=format(p,a[1]);if(!source.supported())return not_possible;const auto bytes=read<std::array<std::uint8_t,16>>(p,a[2]);const auto destination=audio::WaveCodecFormat::parse(bytes);if(((a[4]&0x10000) && destination.tag!=1) || ((a[4]&0x20000) && destination.channels!=source.channels) || ((a[4]&0x40000) && destination.rate!=source.rate) || ((a[4]&0x80000) && destination.bits!=16))return not_possible;
            p.memory.check(a[2],18,Memory::Write);p.memory.store(a[2],1,16);p.memory.store(a[2]+2,source.channels,16);p.memory.store(a[2]+4,source.rate,32);p.memory.store(a[2]+8,source.rate*source.channels*2,32);p.memory.store(a[2]+12,source.channels*2,16);p.memory.store(a[2]+14,16,16);p.memory.store(a[2]+16,0,16);return U32(0);});
        api(p,"msacm32.dll","acmStreamOpen",8,[this](Process&,Args a){if(a[1])return invalid_handle;if(a[4] || a[5] || (a[7]&~5u))throw GuestFault(FaultKind::unsupported,p.cpu.eip,"ACM asynchronous/filter stream is outside this source profile");
            if(a[0]){p.memory.check(a[0],4,Memory::Write);p.memory.store(a[0],0,32);}auto source=format(p,a[2]),destination=format(p,a[3]);if(!source.supported() || !destination.supported() || destination.tag!=1 || destination.bits!=16 || source.rate!=destination.rate || source.channels!=destination.channels)return not_possible;
            if(a[7]&1)return U32(0);if(!a[0])return invalid_parameter;if(streams.size()>=64)return U32(4);auto state=std::make_unique<Stream>(std::move(source),std::move(destination));const auto id=p.state().next_handle;p.state().next_handle+=16;streams.emplace(id,std::move(state));p.memory.store(a[0],id,32);++opened;return U32(0);});
        api(p,"msacm32.dll","acmStreamSize",4,[this](Process&,Args a){const auto* s=stream(a[0]);if(!s)return invalid_handle;if(a[3]>1)return invalid_parameter;const auto output_block=std::uint64_t(s->source.block_frames())*s->destination.block;
            const auto size=a[3]?((std::uint64_t(a[1])+output_block-1)/output_block)*s->source.block:((std::uint64_t(a[1])+s->source.block-1)/s->source.block)*output_block;if(size>0xffffffffull)return invalid_parameter;p.memory.store(a[2],U32(size),32);return U32(0);});
        api(p,"msacm32.dll","acmStreamPrepareHeader",3,[this](Process&,Args a){return prepare(a);});
        api(p,"msacm32.dll","acmStreamConvert",3,[this](Process&,Args a){return convert(a);});
        api(p,"msacm32.dll","acmStreamUnprepareHeader",3,[this](Process&,Args a){auto* s=stream(a[0]);if(!s)return invalid_handle;if(a[2])return invalid_parameter;if(!s->headers.contains(a[1]))return U32(0);p.memory.check(a[1],84,Memory::Read|Memory::Write);p.memory.store(a[1]+4,p.memory.load(a[1]+4,32)&~0x20000u,32);s->headers.erase(a[1]);return U32(0);});
        api(p,"msacm32.dll","acmStreamClose",2,[this](Process&,Args a){auto* s=stream(a[0]);if(!s)return invalid_handle;if(a[1])return invalid_parameter;if(!s->headers.empty())return U32(513);streams.erase(a[0]);return U32(0);});
    }
};
}
void install_acm(wr::Process& p){auto state=std::make_shared<Acm>(p);state->install();p.state().shutdown.push_back([state]{state->shutdown();});p.state().reports["acm"]=[state]{return state->report();};}
}
