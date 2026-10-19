// One conversion at the old stream-decoder ABI; all packet/PCM work is native.
#include "native_vorbis.hpp"
#include "native_backend.hpp"
#include "guest_api.hpp"
#include "vorbis_stream.hpp"
#include "clock.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <sstream>
#include <vector>
namespace wr {namespace {
struct DecoderState {
    struct Stream {std::unique_ptr<wa2::audio::VorbisStream> codec{std::make_unique<wa2::audio::VorbisStream>()};std::vector<std::uint8_t> input_copy,output_copy;bool finished{};};
    std::map<U32,Stream> streams;bool enabled{true};
    std::uint64_t constructors{},format_queries{},cached_format_queries{},copied_calls{},requests{},calls{},input_bytes{},output_bytes{},resets{},restarts{},exhausted{},errors{},declined{},total_ns{},maximum_ns{};
    bool supported(Process& p,U32 pc){return enabled && p.input_sha256=="246a263b965147a274fa59cdd7a72f6be10ba063d8cf4e61e74baefb56f29327" && wa2_native_code_verified(p.memory,pc);}
    std::string report() const {std::ostringstream out;std::size_t buffered=0;for(const auto& [at,s]:streams)buffered+=s.codec->buffered_bytes();
        out<<"{\"backend\":\"portable-vorbis\",\"enabled\":"<<(enabled?"true":"false")<<",\"constructors\":"<<constructors<<",\"format_queries\":"<<format_queries<<",\"cached_format_queries\":"<<cached_format_queries<<",\"copied_calls\":"<<copied_calls<<",\"requests\":"<<requests<<",\"calls\":"<<calls<<",\"input_bytes\":"<<input_bytes<<",\"output_bytes\":"<<output_bytes<<",\"live_streams\":"<<streams.size()<<",\"buffered_bytes\":"<<buffered<<",\"resets\":"<<resets<<",\"restarts\":"<<restarts<<",\"exhausted_calls\":"<<exhausted<<",\"errors\":"<<errors<<",\"declined\":"<<declined<<",\"total_ns\":"<<total_ns<<",\"max_call_ns\":"<<maximum_ns<<'}';return out.str();}
};
thread_local std::map<Process*,std::weak_ptr<DecoderState>> states;
bool overlaps(U32 a,std::uint64_t an,U32 b,std::uint64_t bn){return std::uint64_t(a)<std::uint64_t(b)+bn && std::uint64_t(b)<std::uint64_t(a)+an;}
}
void install_wa2_native_vorbis(Process& p,bool enabled){
    if(const auto found=states.find(&p);found!=states.end())if(const auto state=found->second.lock()){state->enabled=enabled;return;}
    const auto state=std::make_shared<DecoderState>();state->enabled=enabled;states[&p]=state;
    p.state().shutdown.push_back([pointer=&p]{states.erase(pointer);});
    p.state().reports["native_vorbis"]=[state]{return state->report();};
    p.register_native_function(0x4566d0,0,[state](Process& q)->std::optional<U32>{
        if(!state->supported(q,0x4566d0))return std::nullopt;
        const auto object=q.cpu.r[ECX];
        static constexpr std::array<std::uint8_t,0x22f8> zero{};
        q.memory.copy_in(object,zero);state->streams.erase(object);++state->constructors;
        return 0u;
    });
    p.register_native_function(0x456700,0,[state](Process& q)->std::optional<U32>{
        if(!state->supported(q,0x456700))return std::nullopt;
        const auto object=q.cpu.r[ECX];
        q.memory.check(object,0x22f8,Memory::Read|Memory::Write);
        state->streams.erase(object);++state->resets;
        // Objects are initialized and owned by the native lifecycle. Opaque
        // legacy codec structures therefore contain no old heap allocations.
        static constexpr std::array<std::uint8_t,0x2c8> zero{};
        q.memory.copy_in(object+0x2008,zero);
        q.memory.store(object+0x22e2,0,8);q.memory.store(object+0x22e3,0,8);q.memory.store(object+0x22e4,0,8);q.memory.store(object+0x2004,0,32);
        for(const auto offset:{0x22e8u,0x22ecu,0x22f0u})q.memory.store(object+offset,0,32);
        return 0u;
    });
    p.register_native_function(0x4567b0,2,[state](Process& q)->std::optional<U32>{
        if(!state->supported(q,0x4567b0))return std::nullopt;
        const auto object=q.cpu.r[ECX],format=q.argument(0),input=q.argument(1);
        ++state->format_queries;
        if(!format)return 0u;
        q.memory.check(object,0x22f8,Memory::Read);q.memory.check(format,18,Memory::Write);
        if(q.memory.load(object+0x22e3,8)){
            std::array<std::uint8_t,18> cached{};q.memory.copy_out(object+0x22d0,cached);q.memory.copy_in(format,cached);
            ++state->cached_format_queries;return 1u;
        }
        if(!input)return 0u;
        std::array<std::uint8_t,58> page{};q.memory.copy_out(input,page);
        try {
            const auto info=wa2::audio::query_vorbis_format(page);
            q.memory.store(format,1,16);q.memory.store(format+2,info.channels,16);
            q.memory.store(format+4,info.rate,32);q.memory.store(format+8,info.rate*info.channels*2,32);
            q.memory.store(format+12,info.channels*2,16);q.memory.store(format+14,16,16);q.memory.store(format+16,0,16);
            return 1u;
        } catch(const wa2::audio::VorbisError&) {return 0u;}
    });
    p.register_native_function(0x456930,5,[state](Process& q)->std::optional<U32>{
        if(!state->supported(q,0x456930))return std::nullopt;
        ++state->requests;
        const auto object=q.cpu.r[ECX],input=q.argument(0),count=q.argument(1),output=q.argument(2),capacity=q.argument(3),written=q.argument(4);
        const auto found=state->streams.find(object);
        auto decline=[&]()->std::optional<U32>{++state->declined;return std::nullopt;};
        if(count>1024*1024 || !capacity || capacity>4*1024*1024 || object>0xffffdd07u ||
           overlaps(object,0x22f8,input,count) || overlaps(object,0x22f8,output,capacity) || overlaps(object,0x22f8,written,4) ||
           overlaps(output,capacity,input,count) || overlaps(output,capacity,written,4) || overlaps(input,count,written,4))return decline();
        q.memory.check(object,0x22f8,Memory::Read|Memory::Write);q.memory.check(input,count,Memory::Read);
        q.memory.check(output,capacity,Memory::Write);q.memory.check(written,4,Memory::Write);
        if(found==state->streams.end() && q.memory.load(object+0x22e4,8))return decline();
        auto scope=q.memory.execution_scope();auto* base=const_cast<std::uint8_t*>(scope.linear_base());
        const auto* pages=scope.native_pages();
        auto executable=[&](U32 at,std::uint64_t size){for(auto end=std::uint64_t(at)+size;at<end;){if((*pages[at>>22])[(at>>12)&1023].permissions&Memory::Execute)return true;const auto next=(std::uint64_t(at)|4095ull)+1;if(next>=end)break;at=U32(next);}return false;};
        if(executable(object,0x22f8) || executable(output,capacity) || executable(written,4))return decline();
        // The legacy non-looping path returns 1 at EOS, resets its decoding
        // phase, then returns 0 on the following empty request. Returning 1
        // forever makes the caller spin while trying to refill a PCM buffer.
        if(!count && (found==state->streams.end() || found->second.finished)){
            q.memory.store(written,0,32);
            if(found!=state->streams.end() && q.memory.load(object+0x22e2,8))return 2u;
            if(found!=state->streams.end())state->streams.erase(found);
            ++state->exhausted;q.memory.store(object+0x22e3,0,8);q.memory.store(object+0x22e4,0,8);q.memory.store(object+0x2004,0,32);
            return 0u;
        }
        auto& stream=state->streams[object];
        if(stream.finished && count){stream.codec=std::make_unique<wa2::audio::VorbisStream>();stream.finished=false;++state->restarts;}
        std::span<const std::uint8_t> source;std::span<std::uint8_t> destination;
        if(base){source={base+input,count};destination={base+output,capacity};}
        else{
            // The source-level codec also works when the compatibility memory
            // has no contiguous arena. This path must not require the retired
            // CPU implementation to be present in the release executable.
            stream.input_copy.resize(count);stream.output_copy.resize(capacity);
            q.memory.copy_out(input,stream.input_copy);source=stream.input_copy;destination=stream.output_copy;++state->copied_calls;
        }
        const auto started=wa2::platform::Clock::counter();
        try {
            const auto result=stream.codec->push(source,destination,count==0);
            if(result.accepted!=count)throw wa2::audio::VorbisError("Vorbis ABI input queue exhausted");
            if(!base)q.memory.copy_in(output,destination.first(result.bytes));
            const auto elapsed=wa2::platform::Clock::counter()-started;++state->calls;state->total_ns+=elapsed;state->maximum_ns=std::max(state->maximum_ns,elapsed);
            state->input_bytes+=count;state->output_bytes+=result.bytes;stream.finished=result.finished;
            q.memory.store(written,U32(result.bytes),32);
            if(result.channels){
                q.memory.store(object+0x21c4,result.channels,32);q.memory.store(object+0x21c8,result.rate,32);
                q.memory.store(object+0x22d0,1,16);q.memory.store(object+0x22d2,result.channels,16);
                q.memory.store(object+0x22d4,result.rate,32);q.memory.store(object+0x22d8,result.rate*result.channels*2,32);
                q.memory.store(object+0x22dc,result.channels*2,16);q.memory.store(object+0x22de,16,16);q.memory.store(object+0x22e0,0,16);
                q.memory.store(object+0x22e3,1,8);
            }
            q.memory.store(object+0x22e4,result.finished && !q.memory.load(object+0x22e2,8)?0:2,8);q.memory.store(object+0x2004,0,32);
            return result.finished && q.memory.load(object+0x22e2,8)?2u:1u;
        } catch(const wa2::audio::VorbisError&) {++state->errors;q.memory.store(written,0,32);return 0u;}
    });
}
}
