#include "resource_archive.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <cstring>

namespace wa2::resource {
namespace {
std::uint32_t u32(const std::uint8_t* p) noexcept {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1])<<8) |
        (std::uint32_t(p[2])<<16) | (std::uint32_t(p[3])<<24);
}
void range(const Source& s, std::uint64_t at, std::uint64_t length) {
    if (at>s.size() || length>s.size()-at) throw Error("resource range outside archive");
}
unsigned fold(unsigned c) noexcept { return c>='A' && c<='Z' ? c+32 : c; }
// Keep the special block out of the ordinary token loop's instruction cache.
[[gnu::noinline]] bool copy_repeated_lzss_group(const std::uint8_t* source,std::uint8_t* target,std::size_t written) noexcept {
    if((source[1]&15)!=15)return false;
    const unsigned offset=source[0]|((source[1]&0xf0)<<4);
    const unsigned distance=unsigned((0xfee + written-offset-1)&4095)+1;
    if(distance>16 || (distance&(distance-1)))return false;
    unsigned different{};
#if defined(__clang__)
#pragma clang loop unroll(full)
#endif
    for(unsigned token=1;token<8;++token) {
        const unsigned expected=(offset+18*token)&4095;
        different|=unsigned(source[token*2])^(expected&255);
        different|=unsigned(source[token*2+1])^(((expected>>4)&0xf0)|15);
    }
    if(different)return false;
    // Eight adjacent full-length matches are one repeated pixel/byte word.
    // Source/destination ranges and complete output history are checked by the
    // caller. Verify all tokens before touching output, including the last one.
    if(distance==1)std::memset(target,target[-1],144);
    else {
        std::array<std::uint8_t,16> word;
        std::memcpy(word.data(),target-distance,distance);
        for(unsigned count=distance;count<16;count*=2)std::memcpy(word.data()+count,word.data(),count);
        for(unsigned at=0;at<144;at+=16)std::memcpy(target+at,word.data(),16);
    }
    return true;
}
class MemberSource final : public Source {
    std::shared_ptr<Source> source_;
    std::uint64_t offset_,size_;
public:
    MemberSource(std::shared_ptr<Source> source,std::uint64_t offset,std::uint64_t size)
        :source_(std::move(source)),offset_(offset),size_(size){}
    std::uint64_t size()const override{return size_;}
    void read_at(std::uint64_t at,std::span<std::uint8_t> bytes)override{
        if(at>size_ || bytes.size()>size_-at)throw Error("resource read outside archive member");
        if(!bytes.empty())source_->read_at(offset_+at,bytes);
    }
};
class MemorySource final : public Source {
    std::vector<std::uint8_t> bytes_;
public:
    explicit MemorySource(std::vector<std::uint8_t> bytes):bytes_(std::move(bytes)){}
    std::uint64_t size()const override{return bytes_.size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> bytes)override{
        if(at>bytes_.size() || bytes.size()>bytes_.size()-at)throw Error("resource read outside expanded member");
        if(!bytes.empty())std::memcpy(bytes.data(),bytes_.data()+std::size_t(at),bytes.size());
    }
};
}
int compare_names(std::string_view a, std::string_view b) noexcept {
    const auto n=std::min(a.size(),b.size());
    for (std::size_t i=0;i<n;++i) {
        const auto x=fold(static_cast<unsigned char>(a[i])),y=fold(static_cast<unsigned char>(b[i]));
        if (x!=y) return x<y ? -1 : 1;
    }
    return a.size()==b.size() ? 0 : a.size()<b.size() ? -1 : 1;
}
void validate_lzss(std::span<const std::uint8_t> source,std::size_t output_bytes) {
    std::size_t input=0,written=0;unsigned flags=0;
    while(written<output_bytes) {
        flags>>=1;
        if(!(flags&0x100)){
            if(input==source.size())throw Error("truncated LZSS flags");flags=source[input++]|0xff00;
            if((flags&255)==255 && source.size()-input>=8 && output_bytes-written>=8){input+=8;written+=8;flags>>=7;continue;}
            // An all-match group has exactly 16 input bytes and at most 144
            // output bytes. Validate its eight lengths together; offsets do
            // not affect length validation. Short/error tails stay below.
            if(!(flags&255) && source.size()-input>=16 && output_bytes-written>=144){
                unsigned length=24;
                for(unsigned token=0;token<8;++token)length+=source[input+token*2+1]&15;
                input+=16;written+=length;flags>>=7;continue;
            }
        }
        const unsigned bytes=(flags&1)?1:2;
        if(bytes>source.size()-input)throw Error("truncated LZSS token");
        const unsigned length=bytes==1?1:(source[input+1]&15)+3;
        if(length>output_bytes-written)throw Error("LZSS resource exceeds declared length");
        written+=length;input+=bytes;
    }
    if(input!=source.size())throw Error("LZSS resource has trailing input");
}
unsigned decode_lzss_dictionary(std::span<const std::uint8_t> source,std::span<std::uint8_t> output,
    std::span<std::uint8_t,4096> ring,unsigned* caller_tail_reads) {
    if(caller_tail_reads)*caller_tail_reads=0;
    std::size_t input=0,written=0;unsigned flags=0;
    auto next=[&]() {
        if(input==source.size()) throw Error("truncated LZSS resource");
        return source[input++];
    };
    while(written<output.size()) {
        flags>>=1;
        if(!(flags&0x100)) {
            flags=next()|0xff00;
            // Eight literals are common in TGA/script data. This copies only
            // bytes inside the declared source and destination, on any CPU.
            if((flags&255)==255 && source.size()-input>=8 && output.size()-written>=8) {
                std::memcpy(output.data()+written,source.data()+input,8);
                input+=8;written+=8;flags>>=7;continue;
            }
            // Eight full matches consume 16 input and 144 output bytes.
            // Short tails and initial caller-owned dictionary reads retain the
            // ordinary path, including its partial-output/error behavior.
            if(!(flags&255) && written>=4096 && source.size()-input>=16 && output.size()-written>=144 &&
                copy_repeated_lzss_group(source.data()+input,output.data()+written,written)) {
                input+=16;written+=144;flags>>=7;continue;
            }
        }
        if(flags&1) {
            output[written++]=next();
        } else {
            const unsigned low=next(),high=next(),offset=low|((high&0xf0)<<4),length=(high&15)+3;
            if(length>output.size()-written) throw Error("LZSS resource exceeds declared length");
            const auto position=unsigned((0xfee + written)&4095);
            const unsigned delta=(position-offset)&4095,distance=delta?delta:4096;
            if(written>=distance) {
                auto* target=output.data()+written;const auto* from=target-distance;
                // A token produces at most 18 bytes. When the full 18-byte
                // destination fits, fixed copies avoid a variable-size memcpy
                // and length-dependent branches. Extra destination bytes are
                // overwritten by later tokens; history only uses [0,written).
                // Reads stay in produced history, or in a preceding store from
                // this sequence. The final tokens use exact bounded copies.
                if(distance>=8 && output.size()-written>=18) {
                    if(distance>=16)std::memcpy(target,from,16);
                    else {std::memcpy(target,from,8);std::memcpy(target+8,from+8,8);}
                    std::memcpy(target+16,from+16,2);
                } else if(distance==1 && output.size()-written>=18)std::memset(target,*from,18);
                else if(distance>=length)std::memcpy(target,from,length);
                else if(distance==1)std::memset(target,*from,length);
                else {
                    // A match can repeat bytes produced by this same token.
                    // Copy the already available prefix, then double it.
                    unsigned copied=distance;std::memcpy(target,from,copied);
                    while(copied<length) {
                        const auto count=std::min(copied,length-copied);
                        std::memcpy(target+copied,target,count);copied+=count;
                    }
                }
            } else {
                // Initial dictionary references can cross into newly produced
                // output, including the 18 caller-owned tail bytes.
                const auto initial=distance-written;
                for(unsigned n=0;n<length;++n){const auto at=(offset+n)&4095;
                    if(n<initial){output[written+n]=ring[at];if(caller_tail_reads && at>=0xfee)*caller_tail_reads|=1u<<(at-0xfee);}
                    else output[written+n]=output[written+n-distance];}
            }
            written+=length;
        }
    }
    if(input!=source.size()) throw Error("LZSS resource has trailing input");
    // The dictionary is the last 4 KiB of output. Materialize it once, instead
    // of writing/masking it for every decoded byte in the hot loop.
    finish_lzss_dictionary(output,ring);return flags>>1;
}
void finish_lzss_dictionary(std::span<const std::uint8_t> output,std::span<std::uint8_t,4096> ring){
    const auto written=output.size(),count=std::min<std::size_t>(written,4096),first=written-count;
    const auto at=std::size_t((0xfee + first)&4095),head=std::min(count,4096-at);
    if(head)std::memcpy(ring.data()+at,output.data()+first,head);
    if(count>head)std::memcpy(ring.data(),output.data()+first+head,count-head);
}
unsigned decode_lzss_dictionary(std::span<const std::uint8_t> source,std::span<std::uint8_t> output,
    std::span<std::uint8_t,4096> ring){
    return decode_lzss_dictionary(source,output,ring,nullptr);
}
void decode_lzss(std::span<const std::uint8_t> source,std::span<std::uint8_t> output,std::uint8_t fill) {
    std::array<std::uint8_t,4096> ring{};std::fill_n(ring.begin(),0xfee,fill);
    decode_lzss_dictionary(source,output,ring);
}
Archive Archive::open(std::shared_ptr<Source> source) {
    if(!source) throw Error("missing resource source");
    std::array<std::uint8_t,16> header{};
    if(source->size()<8) throw Error("truncated resource header");
    source->read_at(0,std::span(header).first(std::min<std::uint64_t>(16,source->size())));
    const bool kcap=std::equal(header.begin(),header.begin()+4,"KCAP"),
        lac=std::equal(header.begin(),header.begin()+4,"LAC\0");
    if(!kcap && !lac) throw Error("unsupported resource archive");
    const unsigned start=kcap?16:8,stride=kcap?44:40;
    if(source->size()<start) throw Error("truncated resource header");
    const auto count=u32(header.data()+(kcap?12:4));
    // Validate arithmetic before allocating an index, including hostile files.
    range(*source,start,std::uint64_t(count)*stride);
    if(count>1000000) throw Error("resource index exceeds supported member count");
    std::vector<std::uint8_t> table(std::size_t(count)*stride);
    source->read_at(start,table);
    Archive result;result.source_=std::move(source);result.members_.reserve(count);result.stored_size_prefix_=kcap;
    const auto data_start=std::uint64_t(start)+table.size();
    for(unsigned i=0;i<count;++i) {
        const auto* record=table.data()+std::size_t(i)*stride;Member member;member.source_index=i;
        if(kcap) {
            const auto flags=u32(record);
            for(unsigned n=0;n<24 && record[4+n];++n)member.name.push_back(char(record[4+n]));
            member.offset=u32(record+36);member.stored_size=u32(record+40);member.compressed=flags==1 && member.stored_size!=0;member.dictionary_fill=32;
            // The original tools left uninitialized flags on zero-length
            // directory markers. They are real entries, not packed payloads.
            if(flags>1 && member.stored_size)throw Error("unsupported KCAP compression flag");
        } else {
            for(unsigned n=0;n<31 && record[n];++n)member.name.push_back(char(record[n]^255));
            if(record[31]>1) throw Error("unsupported LAC compression flag");
            member.compressed=record[31]!=0;member.stored_size=u32(record+32);member.offset=u32(record+36);
        }
        range(*result.source_,member.offset,member.stored_size);
        if(member.offset<data_start || (member.compressed && member.stored_size<(kcap?8u:4u)))
            throw Error("invalid resource member range");
        if(kcap && member.compressed) {
            // Payload headers are validated on read. Opening an archive only
            // reads its directory, rather than seeking to every packed file.
            member.offset+=4;member.stored_size-=4;
        }
        result.members_.push_back(std::move(member));
    }
    // Several shipped font and UI archives have unsorted physical records.
    // Normalize the directory using the game's byte-wise ASCII comparator.
    std::stable_sort(result.members_.begin(),result.members_.end(),[](const Member& a,const Member& b) {
        return compare_names(a.name,b.name)<0;
    });
    return result;
}
std::size_t Archive::find(std::string_view name) const noexcept {
    // Match the original midpoint choice for archives with duplicate names.
    std::size_t low=0,high=members_.size();
    while(low<high) {
        const auto mid=low+(high-low)/2;const auto order=compare_names(members_[mid].name,name);
        if(!order)return mid;
        if(order<0)low=mid+1;else high=mid;
    }
    return missing;
}
std::size_t Archive::find_first(std::string_view name) const noexcept {
    const auto found=std::lower_bound(members_.begin(),members_.end(),name,[](const Member& member,std::string_view key){return compare_names(member.name,key)<0;});
    return found!=members_.end() && compare_names(found->name,name)==0 ? std::size_t(found-members_.begin()) : missing;
}
StoredResource Archive::read(std::size_t index,std::size_t maximum) const {
    if(index>=members_.size()) throw Error("invalid resource member index");
    const auto& member=members_[index];
    if(member.stored_size>maximum) throw Error("stored resource exceeds memory limit");
    if(!member.compressed) {
        StoredResource result;result.bytes.resize(member.stored_size);result.expanded_size=member.stored_size;
        source_->read_at(member.offset,result.bytes);return result;
    }
    std::array<std::uint8_t,8> header{};
    source_->read_at(member.offset-(stored_size_prefix_?4:0),std::span(header).first(stored_size_prefix_?8:4));
    if(stored_size_prefix_) {
        const auto declared=u32(header.data());
        if(std::uint64_t(declared)!=std::uint64_t(member.stored_size)+4 && std::uint64_t(declared)+8!=std::uint64_t(member.stored_size)+4)
            throw Error("KCAP stored length mismatch");
    }
    const auto expanded=u32(header.data()+(stored_size_prefix_?4:0));
    if(expanded>maximum) throw Error("expanded resource exceeds memory limit");
    StoredResource result;result.bytes.resize(member.stored_size-4);result.expanded_size=expanded;
    result.compressed=true;result.dictionary_fill=member.dictionary_fill;
    source_->read_at(member.offset+4,result.bytes);return result;
}
std::vector<std::uint8_t> Archive::load(std::size_t index,std::size_t maximum,std::optional<std::uint8_t> fill) const {
    auto stored=read(index,maximum);if(!stored.compressed)return std::move(stored.bytes);
    std::vector<std::uint8_t> output(stored.expanded_size);
    decode_lzss(stored.bytes,output,fill.value_or(stored.dictionary_fill));return output;
}
std::shared_ptr<Source> Archive::open_member(std::size_t index,std::size_t maximum)const{
    if(index>=members_.size())throw Error("invalid resource member index");
    const auto& member=members_[index];
    if(member.compressed)return std::make_shared<MemorySource>(load(index,maximum));
    if(member.stored_size>maximum)throw Error("resource member exceeds memory limit");
    return std::make_shared<MemberSource>(source_,member.offset,member.stored_size);
}
}
