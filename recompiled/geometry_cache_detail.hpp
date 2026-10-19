#pragma once
#include "geometry_cache.hpp"
#include "../engine/byte_equality.hpp"
#include <winrecomp/runtime.hpp>
#include <cstring>
#include <bitset>
#include <span>
#include <vector>
namespace wa2::aot::detail {
struct GeometryRange {unsigned offset,size;};
template<unsigned Bytes>struct GeometryTrace {
    std::bitset<Bytes> reads,writes;
    void read(unsigned offset,unsigned size){for(unsigned n=offset;n<offset+size;++n)if(!writes[n])reads[n]=true;}
    void write(unsigned offset,unsigned size){for(unsigned n=offset;n<offset+size;++n)writes[n]=true;}
    static std::vector<GeometryRange> ranges(const std::bitset<Bytes>& mask){
        std::vector<GeometryRange> result;for(unsigned at=0;at<Bytes;){if(!mask[at]){++at;continue;}
            const auto begin=at;while(at<Bytes && mask[at])++at;result.push_back({begin,at-begin});}return result;
    }
    std::vector<GeometryRange> inputs(unsigned total)const{auto result=ranges(reads);if(total>Bytes)result.push_back({Bytes,total-Bytes});return result;}
    std::vector<GeometryRange> outputs()const{return ranges(writes);}
};
// Memoize only a proved, call-free geometry transaction. The full admitted
// input is compared byte for byte after hashing; hashes never prove equality.
// Cached results include every scratch byte and the complete changed x87 /
// integer state. Unchanged segment and SIMD state remain the caller's own.
// This contains no instructions, dispatch table, resource handles or pixels.
template<unsigned Bytes,unsigned Ways,unsigned Buckets,unsigned CoreOffset=0,unsigned RegistersUsed=255,bool CaptureOnReuse=false>class GeometryCache {
    static_assert(CoreOffset<=Bytes && Ways && Buckets);
    struct Registers {
        std::array<wr::U32,8> r;
        wr::U32 eip,flags,defined;
        std::array<std::uint8_t,160> x87;
        explicit Registers(const wr::Cpu& cpu):r(cpu.r),eip(cpu.eip),flags(cpu.flags),defined(cpu.defined_flags){
            for(unsigned n=0;n<8;++n)if(!(RegistersUsed&(1u<<n)))r[n]=0;
            std::memcpy(x87.data(),cpu.fp.bytes.data(),x87.size());
        }
        bool same(const Registers& other)const{return wa2::memory::equal_words(r,other.r) && eip==other.eip && flags==other.flags && defined==other.defined && wa2::memory::equal_bytes<160>(x87.data(),other.x87.data());}
        void restore(wr::Cpu& cpu)const{for(unsigned n=0;n<8;++n)if(RegistersUsed&(1u<<n))cpu.r[n]=r[n];cpu.eip=eip;cpu.flags=flags;cpu.defined_flags=defined;std::memcpy(cpu.fp.bytes.data(),x87.data(),x87.size());}
    };
    struct Entry {
        std::uint64_t fingerprint{};
        Registers before,after;
        std::array<std::uint8_t,Bytes> input{};
        std::vector<std::uint8_t> output;
        std::vector<GeometryRange> reads,writes;
        Entry(std::uint64_t hash,const Registers& state,std::span<const std::uint8_t,Bytes> bytes,bool capture=true):fingerprint(hash),before(state),after(state){if(capture)std::memcpy(input.data(),bytes.data(),Bytes);}
        void reset(std::uint64_t hash,const Registers& state,std::span<const std::uint8_t,Bytes> bytes,bool capture=true){fingerprint=hash;before=state;after=state;if(capture)std::memcpy(input.data(),bytes.data(),Bytes);output.clear();}
    };
    struct Bucket {unsigned next{};std::vector<Entry> entries;};
    std::array<Bucket,Buckets> buckets_{};
    static std::uint64_t hash(std::span<const std::uint8_t> bytes,std::uint64_t value){
        while(bytes.size()>=8){std::uint64_t word;std::memcpy(&word,bytes.data(),8);value=(value^word)*0x9e3779b185ebca87ull;bytes=bytes.subspan(8);}
        for(auto byte:bytes)value=(value^byte)*0x100000001b3ull;return value;
    }
    static std::uint64_t fingerprint(const Registers& state,std::span<const std::uint8_t,Bytes> bytes){
        auto value=hash(bytes.subspan(CoreOffset),0xcbf29ce484222325ull);
        value=hash({reinterpret_cast<const std::uint8_t*>(state.r.data()),sizeof(state.r)},value);
        value=hash(state.x87,value);return value^(std::uint64_t(state.eip)<<32)^state.flags^state.defined;
    }
public:
    struct Pending {unsigned bucket,index;bool capture{true};};
    bool lookup(wr::Cpu& cpu,std::span<const std::uint8_t,Bytes> input,std::span<std::uint8_t> output,Pending& pending){
        const Registers state(cpu);const auto code=fingerprint(state,input);const auto index=unsigned((code^(code>>32))%Buckets);auto& bucket=buckets_[index];
        for(unsigned n=0;n<bucket.entries.size();++n){auto& entry=bucket.entries[n];if(entry.fingerprint!=code || !entry.before.same(state))continue;
            if constexpr(CaptureOnReuse)if(entry.output.empty()){
                entry.reset(code,state,input);pending={index,n,true};return false;
            }
            if(entry.output.size()!=output.size())continue;
            bool same=true;for(const auto range:entry.reads)if(!wa2::memory::equal_bytes({entry.input.data()+range.offset,range.size},{input.data()+range.offset,range.size})){same=false;break;}
            if(!same)continue;
            for(const auto range:entry.writes)std::memcpy(output.data()+range.offset,entry.output.data()+range.offset,range.size);
            entry.after.restore(cpu);return true;
        }
        // A failed calculation must never leave a reusable partial result.
        if(bucket.entries.empty())bucket.entries.reserve(Ways);
        // A first-use fingerprint is only an admission hint, never a reusable
        // result. On rapidly changing lines avoid copying the complete scratch
        // frame (over 6 KiB for grids). The second observation snapshots the
        // CURRENT complete input before calculation/commit, as above.
        if(bucket.entries.size()<Ways){pending={index,unsigned(bucket.entries.size()),!CaptureOnReuse};bucket.entries.emplace_back(code,state,input,!CaptureOnReuse);}
        else{pending={index,bucket.next++%Ways,!CaptureOnReuse};bucket.entries[pending.index].reset(code,state,input,!CaptureOnReuse);}
        return false;
    }
    void commit(Pending pending,const wr::Cpu& cpu,std::span<const std::uint8_t> output){
        const std::array<GeometryRange,1> reads{{{0,Bytes}}},writes{{{0,unsigned(output.size())}}};commit(pending,cpu,output,reads,writes);
    }
    void commit(Pending pending,const wr::Cpu& cpu,std::span<const std::uint8_t> output,std::span<const GeometryRange> reads,std::span<const GeometryRange> writes){
        auto& entry=buckets_[pending.bucket].entries[pending.index];entry.after=Registers(cpu);entry.output.assign(output.begin(),output.end());
        entry.reads.assign(reads.begin(),reads.end());entry.writes.assign(writes.begin(),writes.end());
    }
};
}
