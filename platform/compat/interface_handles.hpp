#pragma once
#include <winrecomp/process.hpp>
#include <array>
#include <map>
#include <vector>

namespace wa2::platform::compat {
// One immutable vtable word per COM instance. Pack words into read-only pages;
// the adapter object map continues to own identity, reference counts and life.
class InterfaceHandles {
    struct Page {wr::U32 table{};std::array<std::uint64_t,16> used{},ever{};};
    struct Bucket {std::vector<wr::U32> free;unsigned slots{};};
    wr::Process& process_;
    std::map<wr::U32,Page> pages_;
    std::map<wr::U32,Bucket> buckets_;
    std::uint64_t allocations_{},reuses_{},commits_{};unsigned live_{};
public:
    explicit InterfaceHandles(wr::Process& process):process_(process){}
    InterfaceHandles(const InterfaceHandles&)=delete;
    ~InterfaceHandles(){clear();}
    wr::U32 acquire(wr::U32 table){
        auto& bucket=buckets_[table];
        if(bucket.free.empty()){
            bucket.free.reserve(bucket.slots+1024); // Release never allocates.
            const auto page=process_.allocate_bytes(4096);
            try{
                std::array<std::uint8_t,4096> data;
                for(unsigned n=0;n<data.size();++n)data[n]=std::uint8_t(table>>((n&3)*8));
                process_.memory.copy_in(page,data);process_.memory.protect(page,4096,wr::Memory::Read);
                pages_.emplace(page,Page{table});
            }catch(...){process_.memory.release(page);throw;}
            for(unsigned n=1024;n;--n)bucket.free.push_back(page+(n-1)*4);
            bucket.slots+=1024;++commits_;
        }
        const auto handle=bucket.free.back();
        if(process_.memory.permissions(handle)!=wr::Memory::Read || process_.memory.load(handle,32)!=table)
            throw wr::GuestFault(wr::FaultKind::unsupported,handle,"read-only native interface slot was modified");
        auto& page=pages_.at(handle&~4095u);const auto slot=(handle&4095u)/4;const auto bit=std::uint64_t(1)<<(slot%64);
        if(page.used[slot/64]&bit)throw std::logic_error("native interface slot is already in use");
        bucket.free.pop_back();if(page.ever[slot/64]&bit)++reuses_;
        page.used[slot/64]|=bit;page.ever[slot/64]|=bit;++live_;++allocations_;return handle;
    }
    void release(wr::U32 handle){
        auto it=pages_.find(handle&~4095u);
        if((handle&3) || it==pages_.end())throw std::logic_error("invalid native interface slot");
        auto& page=it->second;const auto slot=(handle&4095u)/4;const auto bit=std::uint64_t(1)<<(slot%64);
        if(!(page.used[slot/64]&bit))throw std::logic_error("native interface slot was released twice");
        buckets_.at(page.table).free.push_back(handle);page.used[slot/64]&=~bit;--live_;
    }
    unsigned live()const{return live_;}
    std::size_t bytes()const{return pages_.size()*4096;}
    std::uint64_t commits()const{return commits_;}
    std::uint64_t reuses()const{return reuses_;}
    std::string report()const{return "{\"live_slots\":"+std::to_string(live_)+",\"retained_bytes\":"+std::to_string(bytes())+
        ",\"slot_allocations\":"+std::to_string(allocations_)+",\"reused_slots\":"+std::to_string(reuses_)+",\"page_allocations\":"+std::to_string(commits_)+"}";}
    void clear()noexcept{
        for(const auto& [at,page]:pages_)try{process_.memory.release(at);}catch(...){}
        pages_.clear();buckets_.clear();live_=0;
    }
};
}
