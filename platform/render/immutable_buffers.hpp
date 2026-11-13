#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>
#include <algorithm>

namespace wa2::graphics {
struct PacketHash {
    std::uint64_t operator()(std::span<const std::uint8_t> a,std::span<const std::uint8_t> b)const{
        std::uint64_t h=0x9e3779b97f4a7c15ull;
        const auto part=[&](auto bytes){h^=bytes.size()+0x517cc1b727220a95ull;
            while(bytes.size()>=8){std::uint64_t word;std::memcpy(&word,bytes.data(),8);h=(h^word)*0xbf58476d1ce4e5b9ull;h^=h>>29;bytes=bytes.subspan(8);}
            for(auto value:bytes)h=(h^value)*0x100000001b3ull;};
        part(a);part(b);return h;
    }
};
// GPU objects are created once and never overwritten. The caller owns GL
// deletion and current bindings. Keys contain no texture/resource references.
template<class Handle,unsigned Entries=512,std::size_t MaximumBytes=2*1024*1024,
         std::size_t MaximumPacket=192*1024,class Hash=PacketHash>
class ImmutableBuffers {
    struct Entry {std::optional<Handle> handle;std::uint64_t hash{},age{};std::vector<std::uint8_t> vertices,indices;};
    struct Seen {std::uint64_t hash{};std::size_t vertices{},indices{};bool valid{};};
    std::array<Entry,Entries> entries_;
    std::array<Seen,2048> seen_{};
    std::uint64_t age_{};std::size_t bytes_{};unsigned count_{};
public:
    std::uint64_t hits{},misses{},creations{},evictions{},admission_waits{};
    static constexpr auto maximum_bytes=MaximumBytes,maximum_packet=MaximumPacket;
    static constexpr auto maximum_entries=Entries;
    static_assert(Entries && MaximumPacket<=MaximumBytes);
    std::size_t bytes()const{return bytes_;}unsigned size()const{return count_;}
    template<class Release>void remove(Entry& entry,Release release){
        release(*entry.handle);bytes_-=entry.vertices.size()+entry.indices.size();--count_;entry=Entry{};
    }
    template<class Release>void clear(Release release){
        for(auto& entry:entries_)if(entry.handle)remove(entry,release);
        seen_={};
    }
    template<class Create,class Release>std::optional<Handle> acquire(std::span<const std::uint8_t> vertices,std::span<const std::uint8_t> indices,Create create,Release release){
        if(vertices.empty() || indices.empty() || vertices.size()>MaximumPacket || indices.size()>MaximumPacket-vertices.size())return {};
        const auto hash=Hash{}(vertices,indices);++age_;
        for(auto& entry:entries_)if(entry.handle && entry.hash==hash && entry.vertices.size()==vertices.size() && entry.indices.size()==indices.size() &&
            std::equal(vertices.begin(),vertices.end(),entry.vertices.begin()) && std::equal(indices.begin(),indices.end(),entry.indices.begin())){
            entry.age=age_;++hits;return entry.handle;
        }
        ++misses;auto& seen=seen_[hash%seen_.size()];
        if(!seen.valid || seen.hash!=hash || seen.vertices!=vertices.size() || seen.indices!=indices.size()){
            seen={hash,vertices.size(),indices.size(),true};++admission_waits;return {};
        }
        Entry incoming;incoming.vertices.assign(vertices.begin(),vertices.end());incoming.indices.assign(indices.begin(),indices.end());
        const auto needed=vertices.size()+indices.size();
        while(count_==Entries || bytes_+needed>MaximumBytes){
            Entry* oldest=nullptr;for(auto& entry:entries_)if(entry.handle && (!oldest || entry.age<oldest->age))oldest=&entry;
            remove(*oldest,release);++evictions;
        }
        incoming.handle=create(vertices,indices);if(!incoming.handle)return {};
        incoming.hash=hash;incoming.age=age_;
        for(auto& entry:entries_)if(!entry.handle){entry=std::move(incoming);bytes_+=needed;++count_;++creations;return entry.handle;}
        return {};
    }
};
}
