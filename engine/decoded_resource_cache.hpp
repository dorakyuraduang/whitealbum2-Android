#pragma once
#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <span>
#include <vector>
#include <algorithm>
namespace wa2::resource {
#if defined(WA2_AOT_DIAGNOSTICS) && WA2_AOT_DIAGNOSTICS
inline thread_local bool decoded_resource_cache_enabled=true;
struct DecodedCacheReferenceScope {bool previous{decoded_resource_cache_enabled};DecodedCacheReferenceScope(){decoded_resource_cache_enabled=false;}~DecodedCacheReferenceScope(){decoded_resource_cache_enabled=previous;}};
#endif
// Read-only archive identity + member number. No filename, OS object or guest
// pointer enters this bounded cache. The adapter invalidates archive identities.
struct DecodedKey {std::uint64_t archive{},member{};bool operator==(const DecodedKey&)const=default;};
struct DecodedMember {
    DecodedKey key;std::vector<std::uint8_t> bytes;bool compressed{};unsigned flags{},tail_reads{};
    std::array<std::uint8_t,18> tail{};
    bool matches(std::span<const std::uint8_t,18> input)const{
        for(unsigned n=0;n<18;++n)if((tail_reads&(1u<<n)) && tail[n]!=input[n])return false;return true;
    }
};
class DecodedResourceCache {
    struct Seen {DecodedKey key;std::uint64_t used{};unsigned count{};};
    std::array<Seen,128> seen_{};std::uint64_t clock_{};
    std::list<std::shared_ptr<const DecodedMember>> entries_;
    void note(DecodedKey key){auto found=std::find_if(seen_.begin(),seen_.end(),[&](const auto& item){return item.used && item.key==key;});
        if(found==seen_.end())found=std::min_element(seen_.begin(),seen_.end(),[](const auto& a,const auto& b){return a.used<b.used;});
        const auto count=found->used && found->key==key?std::min(found->count+1,2u):1u;*found={key,++clock_,count};}
public:
    static constexpr std::size_t MaximumBytes=8u*1024*1024,MaximumEntryBytes=4u*1024*1024,MaximumEntries=32;
    struct Stats {std::uint64_t hits{},misses{},captures{},evictions{},bytes{};} stats;
    std::shared_ptr<const DecodedMember> find(DecodedKey key,std::span<const std::uint8_t,18> tail){
#if defined(WA2_AOT_DIAGNOSTICS) && WA2_AOT_DIAGNOSTICS
        if(!decoded_resource_cache_enabled){++stats.misses;return {};}
#endif
        for(auto at=entries_.begin();at!=entries_.end();++at)if((*at)->key==key && (*at)->matches(tail)){
            auto result=*at;entries_.splice(entries_.begin(),entries_,at);++stats.hits;return result;}
        ++stats.misses;note(key);return {};
    }
    bool capture_ready(DecodedKey key,std::size_t bytes)const{
#if defined(WA2_AOT_DIAGNOSTICS) && WA2_AOT_DIAGNOSTICS
        if(!decoded_resource_cache_enabled)return false;
#endif
        if(!bytes || bytes>MaximumEntryBytes)return false;
        return std::any_of(seen_.begin(),seen_.end(),[&](const auto& item){return item.used && item.key==key && item.count>=2;});
    }
    void capture(DecodedKey key,std::span<const std::uint8_t> bytes,bool compressed,unsigned flags,unsigned tail_reads,std::span<const std::uint8_t,18> tail){
        if(!capture_ready(key,bytes.size()))return;
        auto item=std::make_shared<DecodedMember>();item->key=key;item->bytes.assign(bytes.begin(),bytes.end());item->compressed=compressed;
        item->flags=flags;item->tail_reads=tail_reads;std::copy(tail.begin(),tail.end(),item->tail.begin());
        for(auto at=entries_.begin();at!=entries_.end();)if((*at)->key==key){stats.bytes-=(*at)->bytes.size();at=entries_.erase(at);}else ++at;
        while(!entries_.empty() && (stats.bytes+bytes.size()>MaximumBytes || entries_.size()>=MaximumEntries)){
            stats.bytes-=entries_.back()->bytes.size();entries_.pop_back();++stats.evictions;}
        entries_.push_front(std::move(item));stats.bytes+=bytes.size();++stats.captures;
    }
    std::size_t size()const{return entries_.size();}
};
}
