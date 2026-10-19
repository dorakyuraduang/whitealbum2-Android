#pragma once
// Header-only consumers must use the same exception model as the runtime.
// Without C++ unwinding, a handled GuestFault can silently retain a mutex lock.
#if defined(_MSC_VER) && !defined(_CPPUNWIND)
#error "WinRecomp requires C++ stack unwinding; compile with /EHsc (including sanitizer builds)."
#endif
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include "winrecomp/native_memory.hpp"

namespace wr {
enum class FaultKind { memory, divide, unsupported, untranslated, budget, breakpoint, floating };
class GuestFault : public std::runtime_error {
public:
    FaultKind kind;
    std::uint32_t address;
    GuestFault(FaultKind k,std::uint32_t a,const std::string& message)
        :std::runtime_error(message+" at guest address "+std::to_string(a)),kind(k),address(a){}
};
// Guest addresses remain 32-bit values. Reservations own sparse 4 KiB pages;
// native backends access backing pages through a locked, checked view.
class Memory {
public:
    using U32=std::uint32_t;
    enum Permission : unsigned { Read=1, Write=2, Execute=4 };
    static constexpr std::size_t PAGE=4096;
    struct RegionInfo { U32 base; std::size_t size; };
    // Stable native-backend view. Valid only while an ExecutionScope holds
    // this memory's lock. Generated code checks length/access on each native
    // memory operand and yields before any executable-page store.
    struct NativePage {std::uint8_t* data{};U32 permissions{},length{};};
    using NativeIndex=std::array<NativePage,1024>;
    static_assert(sizeof(NativePage)==16);
private:
    struct PageDelete {
        NativeMemoryArena* arena{};U32 at{};
        bool decommitted{};
        void operator()(std::array<std::uint8_t,PAGE>* bytes) const noexcept {
            if(arena){bytes->~array();if(!decommitted)arena->decommit(at);}else delete bytes;
        }
    };
    using PageData=std::unique_ptr<std::array<std::uint8_t,PAGE>,PageDelete>;
    struct Page { PageData data{nullptr,PageDelete{}}; unsigned permissions{}; std::size_t length{PAGE}; const void* static_signature{};bool static_matches{},blocks_linear{};NativePage* native{}; };
    struct Region { U32 base; std::size_t size; std::vector<Page> pages; };
    // Declared before regions so committed page owners die before the arena.
    std::unique_ptr<NativeMemoryArena> linear_arena_;
    std::size_t linear_blockers_{};
    std::map<U32,Region> regions_;
    // Sparse two-level index for aligned reservations. Entries refer to page
    // metadata, so commit/decommit/protect never leave cached host pointers or
    // cached permissions behind. Unaligned and crossing accesses use spans().
    using PageIndex=std::array<Page*,1024>;
    std::array<std::unique_ptr<PageIndex>,1024> page_index_{};
    std::array<std::unique_ptr<NativeIndex>,1024> native_indices_{};
    std::array<NativeIndex*,1024> native_roots_{};
    mutable std::recursive_mutex mutex_;
    std::uint64_t code_epoch_{};
    const void* native_proof_owner_{};
    std::uint64_t native_proof_epoch_{~std::uint64_t(0)};
    std::vector<std::uint8_t> native_proof_pages_;
    void invalidate_code(Page& page) {
        if(page.static_signature || (page.permissions&Execute)){page.static_signature=nullptr;++code_epoch_;}
    }
    void refresh_native(Page& page){
        if(page.native)*page.native={page.data?page.data->data():nullptr,U32(page.permissions),U32(page.length)};
        const bool blocked=linear_arena_ && page.data && (!page.native || page.length!=PAGE ||
            ((page.permissions&Write) && (!(page.permissions&Read) || (page.permissions&Execute))) ||
            ((page.permissions&Execute) && !(page.permissions&Read)));
        if(blocked!=page.blocks_linear){if(blocked)++linear_blockers_;else --linear_blockers_;page.blocks_linear=blocked;}
    }
    void initialize_page(Page& page,std::size_t offset,std::span<const std::uint8_t> bytes){
        if(auto* arena=page.data.get_deleter().arena)arena->initialize(page.data.get_deleter().at,offset,bytes,page.permissions);
        else std::copy_n(bytes.data(),bytes.size(),page.data->data()+offset);
    }
    inline static thread_local Memory* execution_owner_{};
    auto access_lock() {
        std::unique_lock<std::recursive_mutex> guard(mutex_,std::defer_lock);
        if(execution_owner_!=this)guard.lock();return guard;
    }
    static void bounds(U32 a,std::size_t n) {
        if(n>0x100000000ull || std::uint64_t(a)+n>0x100000000ull)
            throw GuestFault(FaultKind::memory,a,"guest range wraps");
    }
    Region& region(U32 a) {
        auto it=regions_.upper_bound(a);
        if(it!=regions_.begin()) {
            auto& r=std::prev(it)->second;
            if(std::uint64_t(a)-r.base<r.size)return r;
        }
        throw GuestFault(FaultKind::memory,a,"unmapped guest memory");
    }
    Region& one_region(U32 a,std::size_t n) {
        bounds(a,n);if(!n)throw GuestFault(FaultKind::memory,a,"empty page range");
        auto& r=region(a);
        if(n>r.size-std::size_t(a-r.base))throw GuestFault(FaultKind::memory,a,"range crosses a reservation");
        return r;
    }
    template<class F> void spans(U32 a,std::size_t n,F&& f) {
        bounds(a,n);
        std::size_t done=0;
        while(done<n) {
            const U32 p=a+U32(done);auto& r=region(p);const std::size_t off=p-r.base;
            auto& page=r.pages.at(off/PAGE);
            const auto len=std::min({n-done,PAGE-off%PAGE,r.size-off});
            f(page,off%PAGE,len,done,p);done+=len;
        }
    }
    void validate(U32 a,std::size_t n,unsigned access) {
        spans(a,n,[&](Page& p,std::size_t,std::size_t,std::size_t,U32 at){
            if(!p.data)throw GuestFault(FaultKind::memory,at,"uncommitted guest memory");
            if((p.permissions&access)!=access)throw GuestFault(FaultKind::memory,at,"guest memory permission fault");
        });
    }
    static void width_check(unsigned w) {
        if(w!=8 && w!=16 && w!=32)throw std::runtime_error("unsupported memory width");
    }
    Page* indexed_page(U32 at,std::size_t n,unsigned access) {
        const auto& index=page_index_[at>>22];
        if(!index)return nullptr;
        auto* page=(*index)[(at>>12)&1023];const auto off=at&(PAGE-1);
        if(!page || off>=page->length || n>page->length-off)return nullptr;
        if(!page->data)throw GuestFault(FaultKind::memory,at,"uncommitted guest memory");
        if((page->permissions&access)!=access)throw GuestFault(FaultKind::memory,at,"guest memory permission fault");
        return page;
    }
    // A failed fast lookup is retried by the ordinary accessor, which owns
    // range validation and precise fault addresses. Only a live execution
    // scope may use this lookup: its lock keeps metadata and bytes stable.
    Page* execution_page(U32 at,std::size_t size,unsigned access) noexcept {
        const auto& index=page_index_[at>>22];if(!index)return nullptr;
        auto* page=(*index)[(at>>12)&1023];const auto offset=at&(PAGE-1);
        return page && page->data && (page->permissions&access)==access &&
            offset<page->length && size<=page->length-offset ? page : nullptr;
    }
    void index_region(Region& r) {
        if(r.base%PAGE)return;
        // Allocate leaves before publishing any entries, for atomic failure.
        for(std::size_t i=0;i<r.pages.size();++i){const auto at=r.base+U32(i*PAGE);auto& leaf=page_index_[at>>22];if(!leaf)leaf=std::make_unique<PageIndex>();auto& native=native_indices_[at>>22];if(!native)native=std::make_unique<NativeIndex>();native_roots_[at>>22]=native.get();}
        for(std::size_t i=0;i<r.pages.size();++i){const auto at=r.base+U32(i*PAGE);auto& page=r.pages[i];page.length=std::min(PAGE,r.size-i*PAGE);(*page_index_[at>>22])[(at>>12)&1023]=&page;page.native=&(*native_indices_[at>>22])[(at>>12)&1023];refresh_native(page);}
    }
    void unindex_region(const Region& r) {
        for(const auto& page:r.pages)if(page.blocks_linear)--linear_blockers_;
        if(r.base%PAGE)return;
        for(std::size_t i=0;i<r.pages.size();++i){const auto at=r.base+U32(i*PAGE);(*page_index_[at>>22])[(at>>12)&1023]=nullptr;*r.pages[i].native={};}
    }
    void discard_pages(Region& r,std::size_t first,std::size_t last) noexcept {
        for(auto i=first;i<=last;){
            auto& page=r.pages[i];auto* arena=page.data.get_deleter().arena;
            auto end=i+1;
            if(page.data && arena){
                while(end<=last && r.pages[end].data && r.pages[end].data.get_deleter().arena==arena)++end;
                arena->decommit_range(r.base+U32(i*PAGE),(end-i)*PAGE);
                for(auto k=i;k<end;++k)r.pages[k].data.get_deleter().decommitted=true;
            }
            for(;i<end;++i){auto& discarded=r.pages[i];invalidate_code(discarded);discarded.data.reset();discarded.permissions=0;refresh_native(discarded);}
        }
    }
public:
    ~Memory(){for(auto& [at,r]:regions_)discard_pages(r,0,r.pages.size()-1);}
    auto lock() { return std::unique_lock<std::recursive_mutex>(mutex_); }
    NativeMemoryArena::Statistics native_operations(){auto guard=lock();return linear_arena_?linear_arena_->statistics():NativeMemoryArena::Statistics{};}
    bool enable_linear_memory(){
        auto guard=lock();if(linear_arena_)return true;
        if(!regions_.empty())throw std::logic_error("linear memory must be enabled before mappings");
        linear_arena_=NativeMemoryArena::create();return bool(linear_arena_);
    }
    // Only translated guest instructions run in this scope. The outer lock
    // still excludes native threads, while their individual loads/stores do
    // not repeatedly acquire the same recursive mutex. Leave before host APIs,
    // waits, scheduler polls or callbacks, and restore nested owners on unwind.
    class ExecutionScope {
        std::unique_lock<std::recursive_mutex> guard_;
        Memory& memory_;
        Memory* previous_;
    public:
        explicit ExecutionScope(Memory& memory):guard_(memory.access_lock()),memory_(memory),previous_(execution_owner_){execution_owner_=&memory;}
        ~ExecutionScope(){execution_owner_=previous_;}
        ExecutionScope(const ExecutionScope&)=delete;
        ExecutionScope& operator=(const ExecutionScope&)=delete;
        const NativeIndex* const* native_pages() const {return memory_.native_roots_.data();}
        std::uint8_t* linear_base() const {return memory_.linear_arena_ && !memory_.linear_blockers_?memory_.linear_arena_->base():nullptr;}
        // A view of the readable suffix of ONE checked backing page. It is
        // valid only until a mapping change and while this scope is alive.
        // Declining is side-effect free; callers retain their precise slow
        // path for crossings, partial reservations and permission faults.
        std::span<const std::uint8_t> readable(U32 at,std::size_t maximum) const {
            if(!maximum)return {};
            const auto* page=memory_.execution_page(at,1,Read);if(!page)return {};
            const auto offset=at%PAGE;
            return {page->data->data()+offset,std::min(maximum,page->length-offset)};
        }
        // Fixed-width translated accesses need no repeated TLS/lock lookup or
        // run-time width loop. Crossing/partial/unmapped ranges still use the
        // exact atomic slow path, and executable stores invalidate proofs.
        template<unsigned Width> [[gnu::always_inline]] U32 load(U32 at) {
            static_assert(Width==8 || Width==16 || Width==32);
            if(auto* page=memory_.execution_page(at,Width/8,Read)){
                const auto* bytes=page->data->data()+at%PAGE;U32 value=bytes[0];
                if constexpr(Width>=16)value|=U32(bytes[1])<<8;
                if constexpr(Width==32)value|=(U32(bytes[2])<<16)|(U32(bytes[3])<<24);
                return value;
            }
            return memory_.load(at,Width);
        }
        template<unsigned Width> [[gnu::always_inline]] void store(U32 at,U32 value) {
            static_assert(Width==8 || Width==16 || Width==32);
            if(auto* page=memory_.execution_page(at,Width/8,Write)){
                memory_.invalidate_code(*page);auto* bytes=page->data->data()+at%PAGE;
                bytes[0]=std::uint8_t(value);
                if constexpr(Width>=16)bytes[1]=std::uint8_t(value>>8);
                if constexpr(Width==32){bytes[2]=std::uint8_t(value>>16);bytes[3]=std::uint8_t(value>>24);}
                return;
            }
            memory_.store(at,value,Width);
        }
        // Floating stores have already checked their complete operand before
        // arithmetic commits. Reuse this locked page view for the write too;
        // crossings still preserve the atomic slow path and code invalidation.
        [[gnu::always_inline]] void write_bytes(U32 at,std::span<const std::uint8_t> bytes){
            if(bytes.empty())return;
            if(auto* page=memory_.execution_page(at,bytes.size(),Write)){
                memory_.invalidate_code(*page);
                std::memcpy(page->data->data()+at%PAGE,bytes.data(),bytes.size());return;
            }
            memory_.copy_in(at,bytes);
        }
    };
    auto execution_scope(){return ExecutionScope(*this);}
    bool available(U32 base,std::size_t size) {
        auto guard=lock();bounds(base,size);if(!size)return false;
        auto it=regions_.lower_bound(base);
        if(it!=regions_.end() && it->first<std::uint64_t(base)+size)return false;
        if(it!=regions_.begin()){const auto& p=std::prev(it)->second;if(std::uint64_t(p.base)+p.size>base)return false;}
        return true;
    }
    void reserve(U32 base,std::size_t size) {
        auto guard=lock();
        if(!size || size>512ull*1024*1024 || !available(base,size))throw GuestFault(FaultKind::memory,base,"invalid or overlapping reservation");
        auto [it,inserted]=regions_.emplace(base,Region{base,size,std::vector<Page>((size+PAGE-1)/PAGE)});
        try{index_region(it->second);}catch(...){regions_.erase(it);throw;}
    }
    void commit(U32 base,std::size_t size,unsigned permissions) {
        auto guard=lock();auto& r=one_region(base,size);
        const auto first=(base-r.base)/PAGE,last=(std::size_t(base-r.base)+size-1)/PAGE;
        // Allocate everything first: allocation failure must not partially commit.
        std::vector<std::pair<std::size_t,PageData>> pending;
        pending.reserve(last-first+1);
        for(auto i=first;i<=last;)if(!r.pages[i].data){
            const auto at=r.base+U32(i*PAGE);
            if(linear_arena_ && r.base%PAGE==0){
                auto end=i+1;while(end<=last && !r.pages[end].data)++end;
                auto* base=linear_arena_->commit_range(at,(end-i)*PAGE,permissions);
                for(;i<end;++i){const auto page_at=r.base+U32(i*PAGE);auto* bytes=::new(base+(page_at-at)) std::array<std::uint8_t,PAGE>;
                    pending.emplace_back(i,PageData(bytes,PageDelete{linear_arena_.get(),page_at}));}
            }else{pending.emplace_back(i,PageData(new std::array<std::uint8_t,PAGE>{},PageDelete{}));++i;}
        }else ++i;
        for(auto& [i,p]:pending){r.pages[i].data=std::move(p);r.pages[i].permissions=permissions;refresh_native(r.pages[i]);}
        // Already-committed pages retain contents AND protection (VirtualAlloc).
    }
    void map(U32 base,std::size_t size,unsigned permissions) {
        auto guard=lock();reserve(base,size);
        try{commit(base,size,permissions);}catch(...){release(base);throw;}
    }
    void release(U32 base) {
        auto guard=lock();auto it=regions_.find(base);if(it==regions_.end())throw GuestFault(FaultKind::memory,base,"release requires allocation base");
        discard_pages(it->second,0,it->second.pages.size()-1);
        unindex_region(it->second);regions_.erase(it);
    }
    void decommit(U32 base,std::size_t size) {
        auto guard=lock();auto& r=one_region(base,size);
        discard_pages(r,(base-r.base)/PAGE,(std::size_t(base-r.base)+size-1)/PAGE);
    }
    unsigned protect(U32 base,std::size_t size,unsigned permissions) {
        auto guard=lock();auto& r=one_region(base,size);validate(base,size,0);
        const auto first=(base-r.base)/PAGE,last=(std::size_t(base-r.base)+size-1)/PAGE;
        const unsigned old=r.pages[first].permissions;
        for(auto i=first;i<=last;++i){auto& page=r.pages[i];
            if(auto* arena=page.data.get_deleter().arena)arena->protect(page.data.get_deleter().at,permissions);
            invalidate_code(page);if((permissions&Execute) && !(page.permissions&Execute))++code_epoch_;page.permissions=permissions;refresh_native(page);
        }
        return old;
    }
    RegionInfo allocation(U32 at) {auto guard=lock();const auto& r=region(at);return {r.base,r.size};}
    unsigned permissions(U32 at) {auto guard=lock();auto& r=region(at);auto& p=r.pages[(at-r.base)/PAGE];if(!p.data)throw GuestFault(FaultKind::memory,at,"uncommitted guest memory");return p.permissions;}
    std::vector<RegionInfo> regions() {auto guard=lock();std::vector<RegionInfo> v;for(const auto& [a,r]:regions_)v.push_back({a,r.size});return v;}
    void check(U32 base,std::size_t size,unsigned access) {auto guard=access_lock();if(size && indexed_page(base,size,access))return;validate(base,size,access);}
    void initialize(U32 base,std::span<const std::uint8_t> bytes) {
        auto guard=access_lock();if(bytes.empty())return;
        if(auto* p=indexed_page(base,bytes.size(),0)){invalidate_code(*p);initialize_page(*p,base%PAGE,bytes);return;}
        validate(base,bytes.size(),0);
        spans(base,bytes.size(),[&](Page& p,std::size_t off,std::size_t n,std::size_t done,U32){invalidate_code(p);initialize_page(p,off,bytes.subspan(done,n));});
    }
    void copy_in(U32 base,std::span<const std::uint8_t> bytes) {
        auto guard=access_lock();if(bytes.empty())return;
        if(auto* p=indexed_page(base,bytes.size(),Write)){invalidate_code(*p);std::copy_n(bytes.data(),bytes.size(),p->data->data()+base%PAGE);return;}
        validate(base,bytes.size(),Write);
        spans(base,bytes.size(),[&](Page& p,std::size_t off,std::size_t n,std::size_t done,U32){invalidate_code(p);std::copy_n(bytes.data()+done,n,p.data->data()+off);});
    }
    void copy_out(U32 base,std::span<std::uint8_t> bytes) {
        auto guard=access_lock();if(bytes.empty())return;
        if(auto* p=indexed_page(base,bytes.size(),Read)){std::copy_n(p->data->data()+base%PAGE,bytes.size(),bytes.data());return;}
        validate(base,bytes.size(),Read);
        spans(base,bytes.size(),[&](Page& p,std::size_t off,std::size_t n,std::size_t done,U32){std::copy_n(p.data->data()+off,n,bytes.data()+done);});
    }
    void fetch(U32 base,std::span<std::uint8_t> bytes) {
        auto guard=access_lock();if(bytes.empty())return;
        if(auto* p=indexed_page(base,bytes.size(),Execute)){std::copy_n(p->data->data()+base%PAGE,bytes.size(),bytes.data());return;}
        validate(base,bytes.size(),Execute);
        spans(base,bytes.size(),[&](Page& p,std::size_t off,std::size_t n,std::size_t done,U32){std::copy_n(p.data->data()+off,n,bytes.data()+done);});
    }
    U32 load(U32 a,unsigned w) {
        auto guard=access_lock();width_check(w);
        if(auto* p=indexed_page(a,w/8,Read)){U32 v=0;const auto off=a%PAGE;for(unsigned i=0;i<w/8;++i)v|=U32((*p->data)[off+i])<<(8*i);return v;}
        validate(a,w/8,Read);
        U32 v=0;spans(a,w/8,[&](Page& p,std::size_t off,std::size_t n,std::size_t done,U32){for(std::size_t i=0;i<n;++i)v|=U32((*p.data)[off+i])<<unsigned(8*(done+i));});return v;
    }
    void store(U32 a,U32 v,unsigned w) {
        auto guard=access_lock();width_check(w);
        if(auto* p=indexed_page(a,w/8,Write)){invalidate_code(*p);const auto off=a%PAGE;for(unsigned i=0;i<w/8;++i)(*p->data)[off+i]=std::uint8_t(v>>(8*i));return;}
        validate(a,w/8,Write);
        spans(a,w/8,[&](Page& p,std::size_t off,std::size_t n,std::size_t done,U32){invalidate_code(p);for(std::size_t i=0;i<n;++i)(*p.data)[off+i]=std::uint8_t(v>>unsigned(8*(done+i)));});
    }
    // The template reference requires an immutable object with static storage;
    // its identity cannot be reused for a different temporary byte sequence.
    template<auto& Expected> bool matches_static_code_page(U32 at) {
        static_assert(std::is_same_v<decltype(Expected),const std::array<std::uint8_t,PAGE>&>);
        auto guard=access_lock();if(at%PAGE)return false;
        auto* page=indexed_page(at,PAGE,Execute);if(!page)return false;
        if(page->static_signature!=&Expected){
            page->static_matches=(*page->data==Expected);page->static_signature=&Expected;
        }
        return page->static_matches;
    }
    std::uint64_t code_epoch(){auto guard=access_lock();return code_epoch_;}
    // The result is held under the same ExecutionScope as native page data.
    // Cache ownership belongs to this Memory, so a destroyed/reused address
    // cannot accidentally reuse another process's code proofs.
    template<class Verify> std::span<const std::uint8_t> native_code_proofs(const void* owner,std::size_t pages,Verify verify) {
        auto guard=access_lock();
        if(native_proof_owner_!=owner || native_proof_epoch_!=code_epoch_ || native_proof_pages_.size()!=pages){
            std::vector<std::uint8_t> replacement(pages);verify(std::span(replacement));
            native_proof_pages_=std::move(replacement);native_proof_owner_=owner;native_proof_epoch_=code_epoch_;
        }
        return native_proof_pages_;
    }
    bool matches_code(U32 at,std::span<const std::uint8_t> expected) {
        auto guard=access_lock();if(expected.empty())return true;
        if(auto* p=indexed_page(at,expected.size(),Execute))return std::equal(expected.begin(),expected.end(),p->data->data()+at%PAGE);
        validate(at,expected.size(),Execute);bool matches=true;
        spans(at,expected.size(),[&](Page& p,std::size_t off,std::size_t n,std::size_t done,U32){if(!std::equal(expected.begin()+done,expected.begin()+done+n,p.data->data()+off))matches=false;});
        return matches;
    }
};
}
