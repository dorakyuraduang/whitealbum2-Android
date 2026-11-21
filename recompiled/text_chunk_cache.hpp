#pragma once
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <memory>
#include <new>
namespace wa2::aot::text_chunks {
using Word=std::uint32_t;
inline constexpr unsigned ChunkSlots=128;
template<class Chunk>class ChunkCache {
    // The old index allows two entries for each of 64 sets. Text segments
    // with the same set compete even when most of the other sets are empty.
    // The longer search retains that total capacity. Its odd stride gives
    // different keys sharing a starting slot independent probe sequences.
    static constexpr unsigned Mask=ChunkSlots-1, Probes=8;
    struct Slot {
        std::unique_ptr<Chunk> chunk;
        Word tag{};
        std::uint64_t used{};
    };
    std::array<Slot,ChunkSlots> slots_;
    std::array<unsigned,64> next_{};
    std::uint64_t clock_{};
public:
    struct Result {Chunk* chunk;bool hit;};
    template<class Matches>
    Result select(Word prior_hash,Word tag,bool dense,Matches matches){
        const unsigned set=prior_hash&63;
        unsigned index=dense?(tag&Mask):set*2;
        const unsigned stride=dense?((tag>>8)|1u):1u;
        const unsigned limit=dense?Probes:2;
        Slot* victim=nullptr;
        std::uint64_t oldest{};
        ++clock_;
        for(unsigned n=0;n<limit;++n,index=(index+stride)&Mask){
            auto& slot=slots_[index];
            if(slot.chunk && slot.chunk->serial && (slot.chunk->geometry_slot<ChunkSlots)==dense &&
               (!dense || slot.tag==tag) && matches(*slot.chunk)){
                slot.used=clock_;
                return {slot.chunk.get(),true};
            }
            if(dense){
                const auto age=!slot.chunk || !slot.chunk->serial?
                    ~std::uint64_t{}:clock_-slot.used;
                if(!victim || age>oldest){victim=&slot;oldest=age;}
            }
        }
        if(!dense)victim=&slots_[set*2+(next_[set]++&1)];
        if(!victim->chunk){
            const int saved=errno;
            victim->chunk.reset(new(std::nothrow) Chunk);
            errno=saved;
            if(!victim->chunk)return {nullptr,false};
        }
        victim->chunk->serial=0;
        victim->chunk->count=0;
        victim->chunk->geometry_slot=dense?static_cast<unsigned>(victim-slots_.data()):ChunkSlots;
        victim->tag=tag;
        victim->used=clock_;
        return {victim->chunk.get(),false};
    }
};
inline std::atomic<std::uint64_t> next_generation{1};
inline std::uint64_t serial(unsigned slot,std::atomic<std::uint64_t>& generations=next_generation){
    // The renderer indexes completed geometry by the low seven bits. Live
    // dense-cache entries have different slots; reusing a slot must also
    // change the full token, including when switching back to the old path.
    auto n=generations.load(std::memory_order_relaxed);
    while(n && n<=~std::uint64_t{}/ChunkSlots){
        if(generations.compare_exchange_weak(n,n+1,std::memory_order_relaxed))
            return n*ChunkSlots+(slot<ChunkSlots?slot:n%ChunkSlots);
    }
    return 0;
}

}
