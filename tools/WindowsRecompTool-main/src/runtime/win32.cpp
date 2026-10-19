#include "winrecomp/process.hpp"
#include "winrecomp/win32_state.hpp"
#include "winrecomp/heap_cache.hpp"
#include "winrecomp/nls_reference.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <limits>
#include <system_error>
#include <cerrno>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/statvfs.h>
#endif
namespace wr {
namespace {
using ApiFunction=std::function<U32(Process&)>;
U32 arg(Process& p,unsigned n){return p.argument(n);}
void kernel(Process& p,const std::string& name,unsigned args,ApiFunction function){p.register_api("kernel32.dll",name,args,std::move(function));}
void zero(Memory& memory,U32 pointer,std::size_t count){std::vector<std::uint8_t> bytes(count);memory.copy_in(pointer,bytes);}
U32 file_error(int value) {
    switch(value){case ENOENT:return 2;case ENOTDIR:return 3;case EACCES:case EPERM:return 5;case EBADF:return 6;
    case ENOMEM:case EMFILE:case ENFILE:return 8;case EEXIST:return 80;case ENOSPC:return 112;case EINVAL:return 87;case EPIPE:return 109;default:return 31;}
}
#ifdef _WIN32
HANDLE file_handle(const Win32State::File& f){return reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(f.stream)));}
#endif
U32 stream_type(std::FILE* file) {
#ifdef _WIN32
    return ::GetFileType(reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(file))));
#else
    struct stat st{};if(fstat(fileno(file),&st))return 0;
    if(S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode))return 3;
    return S_ISCHR(st.st_mode)?2u:1u;
#endif
}
std::filesystem::path guest_path(Process& p,const std::string& name) {
    std::string local=name;std::replace(local.begin(),local.end(),'\\','/');
    if(!p.state().current_directory.empty() && p.state().current_directory.find(':')!=std::string::npos && !local.empty() && local.front()!='/' && local.find(':')==std::string::npos)local=p.state().current_directory+"/"+local;
    local=std::filesystem::path(local).lexically_normal().generic_string();
    auto folded=local;for(auto& ch:folded)ch=char(std::tolower(static_cast<unsigned char>(ch)));
    for(const auto& [key,grant]:p.state().selected_paths)if(folded==key || (grant.directory && folded.starts_with(key+"/"))){
        // A selection may have an ASCII virtual alias. Native Unicode paths
        // stay host-side, just like native handles. Old canonical path grants
        // still work because an exact match has an empty relative suffix.
        const auto suffix=local.size()==key.size()?std::string{}:local.substr(key.size()+1);
        const auto root=std::filesystem::weakly_canonical(grant.path);
        auto result=std::filesystem::weakly_canonical(suffix.empty()?root:root/std::filesystem::path(suffix));
        auto canonical=result.generic_string(),allowed=root.generic_string();
        for(auto* value:{&canonical,&allowed})for(auto& ch:*value)ch=char(std::tolower(static_cast<unsigned char>(ch)));
        if(canonical==allowed || (grant.directory && canonical.starts_with(allowed+"/")))return result;
        throw GuestFault(FaultKind::unsupported,p.cpu.eip,"path escapes selected native folder");
    }
    if(folded=="c:/winrecomp")local=".";
    else if(folded.starts_with("c:/winrecomp/"))local=local.substr(13);
    else if(!p.state().current_directory.empty() && !local.empty() && local.front()!='/' && local.find(':')==std::string::npos)local=p.state().current_directory+"/"+local;
    if(local.empty() || local.find(':')!=std::string::npos || local.starts_with('/'))throw GuestFault(FaultKind::unsupported,p.cpu.eip,"absolute guest paths are not enabled");
    auto result=std::filesystem::weakly_canonical(p.options.data_root/std::filesystem::path(local));
    auto root=p.options.data_root.begin(),file=result.begin();
    for(;root!=p.options.data_root.end();++root,++file)if(file==result.end() || *file!=*root)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"guest path escapes configured data root");
    return result;
}
std::string mounted_name(Process& p,const std::filesystem::path& path){
    const auto name=path.lexically_relative(p.options.data_root);if(name.empty())return "../";return name.generic_string();
}
std::optional<ReadOnlyFileInfo> mounted_info(Process& p,const std::filesystem::path& path){
    return p.state().read_only_files?p.state().read_only_files->info(mounted_name(p,path)):std::nullopt;
}
bool mounted_pattern(std::string pattern,std::string name){
    if(pattern=="*.*")pattern="*";for(auto* text:{&pattern,&name})for(auto& c:*text)if(c>='A' && c<='Z')c=char(c+32);
    std::vector<bool> row(name.size()+1);row[0]=true;
    for(auto c:pattern){std::vector<bool> next(name.size()+1);if(c=='*'){next[0]=row[0];for(std::size_t n=1;n<=name.size();++n)next[n]=row[n]||next[n-1];}
        else for(std::size_t n=1;n<=name.size();++n)next[n]=row[n-1]&&(c=='?' || c==name[n-1]);row.swap(next);}
    return row.back();
}
void mounted_find_data(Process& p,U32 at,const ReadOnlyFileInfo& info){
    if(info.name.size()>=260)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"mounted filename exceeds source find-data buffer");
    std::array<std::uint8_t,320> data{};auto word=[&](unsigned offset,U32 value){for(unsigned n=0;n<4;++n)data[offset+n]=std::uint8_t(value>>(n*8));};
    word(0,info.attributes);word(20,U32(info.modified_100ns));word(24,U32(info.modified_100ns>>32));word(28,U32(info.size>>32));word(32,U32(info.size));
    std::copy(info.name.begin(),info.name.end(),data.begin()+44);p.memory.copy_in(at,data);
}
U32 heap_create(Process& p,U32 flags,U32 initial,U32 maximum) {
    if(flags&~0x00000001u || (maximum && initial>maximum)){p.set_error(87);return 0;}
    // Heap layout is guest-owned, but the documented page-rounded maximum is
    // an upper bound for both in-place and moving reallocations.
    const auto rounded=(std::uint64_t(maximum)+4095u)&~std::uint64_t(4095u);
    if(rounded>0xffffffffu){p.set_error(8);return 0;}
    const auto id=p.state().next_heap;p.state().next_heap+=16;p.state().heaps[id]={U32(rounded),{}};return id;
}
U32 owned_allocation(Process& p,U32 size) {
    const auto pointer=p.virtual_alloc(0,std::max(size,1u),0x3000,0x04);
    // This reservation belongs to an API allocator, not the guest VirtualAlloc
    // caller. Only the corresponding Heap/Global/Local free may release it.
    if(pointer)p.state().virtual_reservations.erase(pointer);
    return pointer;
}
constexpr U32 HEAP_POOL_BYTES=65536,HEAP_SMALL_BYTES=16384;
constexpr unsigned HEAP_IDLE_POOL_LIMIT=16;
constexpr U32 HEAP_IDLE_LARGE_BYTES=4u*1024*1024;
struct LargeHeapCache {
    bool enabled{true};LargeHeapCacheStatistics stats;
    std::map<const Win32State::Heap*,std::map<U32,U32>> heaps;
};
thread_local std::map<Process*,std::weak_ptr<LargeHeapCache>> large_heap_caches;
LargeHeapCache& large_heap_cache(Process& p){
    if(auto found=large_heap_caches.find(&p);found!=large_heap_caches.end())if(auto value=found->second.lock())return *value;
    for(auto at=large_heap_caches.begin();at!=large_heap_caches.end();)if(at->second.expired())at=large_heap_caches.erase(at);else ++at;
    auto cache=std::make_shared<LargeHeapCache>();
    p.state().reports["large_heap_cache"]=[cache]{const auto& s=cache->stats;
        return "{\"idle_blocks\":"+std::to_string(s.idle_large_blocks)+",\"idle_bytes\":"+std::to_string(s.idle_large_bytes)+
            ",\"maximum_idle_bytes\":4194304,\"maximum_idle_blocks\":2,\"peak_idle_bytes\":"+std::to_string(s.peak_idle_large_bytes)+
            ",\"reuses\":"+std::to_string(s.large_block_reuses)+",\"retentions\":"+std::to_string(s.large_block_retained)+
            ",\"evictions\":"+std::to_string(s.large_block_evictions)+"}";};
    large_heap_caches[&p]=cache;return *cache;
}
bool heap_range_writable(Process& p,U32 base,U32 bytes){
    auto scope=p.memory.execution_scope();
    for(U32 offset=0;offset<bytes;offset+=4096){
        const auto at=base+offset;const auto* table=scope.native_pages()[at>>22];if(!table)return false;
        const auto& page=(*table)[(at>>12)&1023];
        if(!page.data || page.length!=4096 || page.permissions!=(Memory::Read|Memory::Write))return false;
    }
    return true;
}
bool heap_pool_writable(Process& p,U32 base){return heap_range_writable(p,base,HEAP_POOL_BYTES);}
void heap_drop_large(Process& p,LargeHeapCache& cache,std::map<U32,U32>& heap,std::map<U32,U32>::iterator block){
    auto& state=cache.stats;p.memory.release(block->first);state.idle_large_bytes-=block->second;
    --state.idle_large_blocks;++state.large_block_evictions;heap.erase(block);
}
void heap_drop_all_large(Process& p,Win32State::Heap& heap){
    const auto entry=large_heap_caches.find(&p);if(entry==large_heap_caches.end())return;
    auto cache=entry->second.lock();if(!cache)return;
    const auto found=cache->heaps.find(&heap);if(found==cache->heaps.end())return;
    while(!found->second.empty())heap_drop_large(p,*cache,found->second,found->second.begin());cache->heaps.erase(found);
}
void heap_drop_idle(Process& p,Win32State::Heap& heap){
    if(!heap.idle_pool)return;
    p.memory.release(heap.idle_pool);heap.pools.erase(heap.idle_pool);heap.idle_pool=0;
    --p.state().idle_heap_pools;++p.state().heap_pool_evictions;
}
U32 heap_extent(U32 size){return (std::max(size,1u)+15u)&~15u;}
void heap_largest(Win32State::HeapPool& pool){pool.largest=0;for(auto [at,size]:pool.free)pool.largest=std::max(pool.largest,size);}
void heap_take(Win32State::HeapPool& pool,std::map<U32,U32>::iterator hole,U32 bytes){
    const auto at=hole->first,left=hole->second-bytes;
    // Reuse the node, so splitting a hole cannot fail after its allocation.
    if(left){auto node=pool.free.extract(hole);node.key()=at+bytes;node.mapped()=left;pool.free.insert(std::move(node));}
    else pool.free.erase(hole);
    heap_largest(pool);
}
std::pair<U32,Win32State::HeapBlock> heap_storage(Process& p,Win32State::Heap& heap,U32 size,U32 flags){
    const auto extent=heap_extent(size);
    if(extent>HEAP_SMALL_BYTES){
        auto& cache=large_heap_cache(p);auto& available=cache.heaps[&heap];
        for(auto item=available.begin();item!=available.end();){
            if(!cache.enabled || !heap_range_writable(p,item->first,item->second))heap_drop_large(p,cache,available,item++);
            else ++item;
        }
        auto best=available.end();
        for(auto item=available.begin();item!=available.end();++item)
            if(item->second>=extent && (best==available.end() || item->second<best->second))best=item;
        if(best!=available.end()){
            const auto at=best->first,capacity=best->second;
            if(flags&8u)zero(p.memory,at,size);
            available.erase(best);auto& state=cache.stats;--state.idle_large_blocks;state.idle_large_bytes-=capacity;++state.large_block_reuses;
            return {at,{size,capacity,0}};
        }
        const auto at=owned_allocation(p,size);return {at,{size,(std::max(size,1u)+4095u)&~4095u,0}};
    }
    auto take=[&](U32 base,Win32State::HeapPool& pool)->U32{
        for(auto hole=pool.free.begin();hole!=pool.free.end();++hole)if(hole->second>=extent){
            const auto at=base+hole->first;if(flags&8u)zero(p.memory,at,size);
            heap_take(pool,hole,extent);
            if(!pool.live){heap.idle_pool=0;--p.state().idle_heap_pools;++p.state().heap_pool_reuses;}
            ++pool.live;return at;
        }
        return 0;
    };
    if(heap.idle_pool){
        if(!p.state().retain_idle_heap_pools || !heap_pool_writable(p,heap.idle_pool))heap_drop_idle(p,heap);
        else {const auto base=heap.idle_pool;auto& pool=heap.pools.at(base);
            if(auto at=take(base,pool))return {at,{size,extent,base}};}
    }
    for(auto& [base,pool]:heap.pools)if(pool.largest>=extent)if(auto at=take(base,pool))return {at,{size,extent,base}};
    const auto base=owned_allocation(p,HEAP_POOL_BYTES);if(!base)return {};
    try{
        Win32State::HeapPool pool;pool.largest=HEAP_POOL_BYTES;pool.free.emplace(0,HEAP_POOL_BYTES);
        auto [found,inserted]=heap.pools.emplace(base,std::move(pool));
        // A new pool is not an idle cache hit. It receives its first live
        // allocation below, while take handles only previously used pools.
        found->second.live=1;
        const auto hole=found->second.free.begin();const auto at=base+hole->first;
        if(flags&8u)zero(p.memory,at,size);heap_take(found->second,hole,extent);
        return {at,{size,extent,base}};
    }catch(...){heap.pools.erase(base);p.memory.release(base);throw;}
}
void heap_release_storage(Process& p,Win32State::Heap& heap,U32 pointer,const Win32State::HeapBlock& block){
    if(!block.pool){
        auto& cache=large_heap_cache(p);auto& state=cache.stats;
        if(cache.enabled && block.capacity<=HEAP_IDLE_LARGE_BYTES && heap_range_writable(p,pointer,block.capacity)){
            try{
                // Tiny earlier temporaries must not permanently occupy the
                // two slots needed by later multi-megabyte image buffers.
                while(state.idle_large_blocks>=2 || state.idle_large_bytes+block.capacity>HEAP_IDLE_LARGE_BYTES){
                    std::map<U32,U32>* owner{};std::map<U32,U32>::iterator victim;auto smallest=block.capacity;
                    for(auto& [h,blocks]:cache.heaps)for(auto at=blocks.begin();at!=blocks.end();++at)
                        if(at->second<smallest){smallest=at->second;owner=&blocks;victim=at;}
                    if(!owner)break;heap_drop_large(p,cache,*owner,victim);
                }
                if(state.idle_large_blocks<2 && state.idle_large_bytes+block.capacity<=HEAP_IDLE_LARGE_BYTES){
                    const auto [at,inserted]=cache.heaps[&heap].emplace(pointer,block.capacity);
                    if(inserted){++state.idle_large_blocks;state.idle_large_bytes+=block.capacity;++state.large_block_retained;
                        state.peak_idle_large_bytes=std::max(state.peak_idle_large_bytes,state.idle_large_bytes);return;}
                }
            }catch(const std::bad_alloc&){} // Free must not depend on a cache allocation.
        }
        p.memory.release(pointer);return;
    }
    auto found=heap.pools.find(block.pool);auto& pool=found->second;
    if(pool.live==1){
        if(p.state().retain_idle_heap_pools && !heap.idle_pool && p.state().idle_heap_pools<HEAP_IDLE_POOL_LIMIT &&
           !pool.free.empty() && heap_pool_writable(p,block.pool)){
            // Keep and repurpose an existing free-list node. The common
            // allocate/draw/free cycle needs neither an OS mapping nor a new
            // host allocation when its last temporary block is returned.
            auto node=pool.free.extract(pool.free.begin());pool.free.clear();node.key()=0;node.mapped()=HEAP_POOL_BYTES;
            pool.free.insert(std::move(node));pool.live=0;pool.largest=HEAP_POOL_BYTES;heap.idle_pool=block.pool;
            auto& state=p.state();++state.idle_heap_pools;++state.heap_pool_retained;
            state.peak_idle_heap_pools=std::max(state.peak_idle_heap_pools,state.idle_heap_pools);return;
        }
        p.memory.release(block.pool);heap.pools.erase(found);return;
    }
    const auto offset=pointer-block.pool;auto [hole,inserted]=pool.free.emplace(offset,block.capacity);
    if(hole!=pool.free.begin()){auto before=std::prev(hole);if(before->first+before->second==hole->first){before->second+=hole->second;pool.free.erase(hole);hole=before;}}
    auto after=std::next(hole);if(after!=pool.free.end() && hole->first+hole->second==after->first){hole->second+=after->second;pool.free.erase(after);}
    --pool.live;heap_largest(pool);
}
U32 heap_allocate(Process& p,U32 heap,U32 flags,U32 size) {
    auto it=p.state().heaps.find(heap);if(it==p.state().heaps.end() || flags&~9u){p.set_error(87);return 0;}
    auto& h=it->second;
    if(size>128u*1024*1024 || (h.maximum && h.requested_bytes+size>h.maximum)){p.set_error(8);return 0;}
    const auto [pointer,block]=heap_storage(p,h,size,flags);if(!pointer)return 0;
    try{h.blocks.emplace(pointer,block);}catch(...){heap_release_storage(p,h,pointer,block);throw;}
    h.requested_bytes+=size;return pointer;
}
U32 heap_free(Process& p,U32 heap,U32 flags,U32 pointer) {
    auto it=p.state().heaps.find(heap);if(it==p.state().heaps.end() || flags&~1u){p.set_error(87);return 0;}
    if(!pointer)return 1;auto& h=it->second;auto block=h.blocks.find(pointer);if(block==h.blocks.end()){p.set_error(87);return 0;}
    const auto size=block->second.size;heap_release_storage(p,h,pointer,block->second);h.blocks.erase(block);h.requested_bytes-=size;return 1;
}
U32 heap_reallocate(Process& p,U32 heap,U32 flags,U32 pointer,U32 size) {
    auto it=p.state().heaps.find(heap);if(it==p.state().heaps.end() || flags&~0x19u){p.set_error(87);return 0;}
    auto& h=it->second;auto block=h.blocks.find(pointer);if(block==h.blocks.end()){p.set_error(87);return 0;}
    const auto prior=block->second.size;const auto total=h.requested_bytes-prior+size;
    if(size>128u*1024*1024 || (h.maximum && total>h.maximum)){p.set_error(8);return 0;}
    if(size<=block->second.capacity){if((flags&8u) && size>prior)zero(p.memory,pointer+prior,size-prior);block->second.size=size;h.requested_bytes=total;return pointer;}
    if(block->second.pool){
        auto& pool=h.pools.at(block->second.pool);auto hole=pool.free.find(pointer-block->second.pool+block->second.capacity);
        const auto extra=heap_extent(size)-block->second.capacity;
        if(hole!=pool.free.end() && hole->second>=extra){
            if((flags&8u) && size>prior)zero(p.memory,pointer+prior,size-prior);
            heap_take(pool,hole,extra);block->second.capacity+=extra;block->second.size=size;h.requested_bytes=total;return pointer;
        }
    }
    if(flags&0x10){p.set_error(8);return 0;}
    const auto [replacement,storage]=heap_storage(p,h,size,flags);if(!replacement)return 0;
    try{
        std::vector<std::uint8_t> data(std::min(prior,size));p.memory.copy_out(pointer,data);p.memory.copy_in(replacement,data);
        h.blocks.emplace(replacement,storage);
    }catch(...){heap_release_storage(p,h,replacement,storage);throw;}
    heap_release_storage(p,h,pointer,block->second);h.blocks.erase(block);h.requested_bytes=total;return replacement;
}
std::vector<std::uint8_t> narrow(Process& p,U32 pointer,int count) {
    if(count==-1){auto s=p.read_string(pointer);s.push_back(0);return {s.begin(),s.end()};}
    if(count<=0 || count>1<<20)throw std::runtime_error("invalid conversion length");std::vector<std::uint8_t> result(std::size_t(count),0);p.memory.copy_out(pointer,result);return result;
}
std::u16string wide(Process& p,U32 pointer,int count) {
    if(count==-1){auto s=p.read_wstring(pointer);s.push_back(0);return s;}
    if(count<=0 || count>1<<20)throw std::runtime_error("invalid conversion length");std::u16string result(std::size_t(count),u'\0');
    p.memory.check(pointer,std::size_t(count)*2,Memory::Read);for(int n=0;n<count;++n)result[std::size_t(n)]=char16_t(p.memory.load(pointer+U32(n*2),16));return result;
}
constexpr std::array<char16_t,32> cp1252={0x20ac,0x0081,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,0x02c6,0x2030,0x0160,0x2039,0x0152,0x008d,0x017d,0x008f,0x0090,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x02dc,0x2122,0x0161,0x203a,0x0153,0x009d,0x017e,0x0178};
U32 multibyte_to_wide(Process& p) {
    const auto cp=arg(p,0)==0?1252u:arg(p,0)==1?437u:arg(p,0),flags=arg(p,1),source=arg(p,2),count=arg(p,3),dest=arg(p,4),capacity=arg(p,5);
    if(!count || ((count&0x80000000u) && count!=0xffffffffu) || (capacity&0x80000000u)){p.set_error(87);return 0;}
    const auto input=narrow(p,source,int(std::bit_cast<std::int32_t>(count)));std::u16string output;
#ifdef _WIN32
    const auto n=MultiByteToWideChar(cp,flags,reinterpret_cast<const char*>(input.data()),int(input.size()),nullptr,0);
    if(!n){p.set_error(GetLastError());return 0;}std::vector<wchar_t> native(std::size_t(n),0);
    if(!MultiByteToWideChar(cp,flags,reinterpret_cast<const char*>(input.data()),int(input.size()),native.data(),n)){p.set_error(GetLastError());return 0;}
    output.assign(native.begin(),native.end());
#else
    if(cp!=0 && cp!=1252)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"portable conversion supports CP_ACP/1252 only");
    if(flags&~1u)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"portable code-page flags not implemented");
    for(auto byte:input)output.push_back(byte>=128 && byte<160?cp1252[byte-128]:char16_t(byte));
#endif
    if(!capacity)return U32(output.size());if(capacity<output.size()){p.set_error(122);return 0;}
    p.memory.check(dest,output.size()*2,Memory::Write);for(std::size_t n=0;n<output.size();++n)p.memory.store(dest+U32(n*2),U32(output[n]),16);return U32(output.size());
}
U32 wide_to_multibyte(Process& p) {
    const auto cp=arg(p,0)==0?1252u:arg(p,0)==1?437u:arg(p,0),flags=arg(p,1),source=arg(p,2),count=arg(p,3),dest=arg(p,4),capacity=arg(p,5),def=arg(p,6),used=arg(p,7);
    if(!count || ((count&0x80000000u) && count!=0xffffffffu) || (capacity&0x80000000u)){p.set_error(87);return 0;}
    const auto input=wide(p,source,int(std::bit_cast<std::int32_t>(count)));std::vector<std::uint8_t> output;bool substituted=false;
    if(used)p.memory.check(used,4,Memory::Write);
#ifdef _WIN32
    static_assert(sizeof(wchar_t)==2);const auto* text=reinterpret_cast<const wchar_t*>(input.data());const char replacement=def?char(p.memory.load(def,8)):0;BOOL native_used=FALSE;
    const auto n=WideCharToMultiByte(cp,flags,text,int(input.size()),nullptr,0,def?&replacement:nullptr,used?&native_used:nullptr);
    if(!n){p.set_error(GetLastError());return 0;}output.resize(std::size_t(n));
    if(!WideCharToMultiByte(cp,flags,text,int(input.size()),reinterpret_cast<char*>(output.data()),n,def?&replacement:nullptr,used?&native_used:nullptr)){p.set_error(GetLastError());return 0;}substituted=native_used!=FALSE;
#else
    if(cp!=0 && cp!=1252)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"portable conversion supports CP_ACP/1252 only");
    const unsigned mode=flags==0?0:flags==0x200?1:flags==0x220?2:flags==0x400?3:4;
    if(mode==4)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"portable wide conversion flags not implemented: "+hex(flags));
    const auto replacement=def?std::uint8_t(p.memory.load(def,8)):std::uint8_t('?');
    for(auto ch:input){const auto* row=nls_encoding(ch);
        if(!row)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"wide conversion outside measured portable CP1252 corpus");
        const bool fallback=row->substituted[mode];output.push_back(fallback?replacement:row->value[mode]);substituted|=fallback;}

#endif
    if(!capacity){if(used)p.memory.store(used,substituted,32);return U32(output.size());}if(capacity<output.size()){p.set_error(122);return 0;}
    p.memory.copy_in(dest,output);if(used)p.memory.store(used,substituted,32);return U32(output.size());
}
U32 classify(Process& p,bool unicode) {
    const auto type=arg(p,unicode?0:1),source=arg(p,unicode?1:2),raw_count=arg(p,unicode?2:3),dest=arg(p,unicode?3:4);
    const auto count=(raw_count&0x80000000u)?0xffffffffu:raw_count;
    if(source==dest || !count){p.set_error(87);return 0;}
#ifdef _WIN32
    if(unicode){const auto input=wide(p,source,int(std::bit_cast<std::int32_t>(count)));std::vector<WORD> result(input.size());
        if(!GetStringTypeW(type,reinterpret_cast<const wchar_t*>(input.data()),int(input.size()),result.data())){p.set_error(GetLastError());return 0;}
        p.memory.check(dest,result.size()*2,Memory::Write);for(std::size_t n=0;n<result.size();++n)p.memory.store(dest+U32(n*2),result[n],16);return 1;}
    const auto input=narrow(p,source,int(std::bit_cast<std::int32_t>(count)));std::vector<WORD> result(input.size());
    if(!GetStringTypeA(arg(p,0),type,reinterpret_cast<const char*>(input.data()),int(input.size()),result.data())){p.set_error(GetLastError());return 0;}
    p.memory.check(dest,result.size()*2,Memory::Write);for(std::size_t n=0;n<result.size();++n)p.memory.store(dest+U32(n*2),result[n],16);return 1;
#else
    std::u16string input;if(unicode)input=wide(p,source,int(std::bit_cast<std::int32_t>(count)));else{for(auto byte:narrow(p,source,int(std::bit_cast<std::int32_t>(count))))input.push_back(byte>=128 && byte<160?cp1252[byte-128]:char16_t(byte));}
    if(type!=1 && type!=2 && type!=4){p.set_error(1004);return 0;}
    std::vector<std::uint16_t> result;result.reserve(input.size());
    for(auto ch:input){const auto* row=nls_row(ch);if(!row)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"character outside measured portable NLS corpus");result.push_back(type==1?row->ctype1:type==2?row->ctype2:row->ctype3);}
    p.memory.check(dest,result.size()*2,Memory::Write);for(std::size_t n=0;n<result.size();++n)p.memory.store(dest+U32(n*2),result[n],16);return 1;
#endif
}
U32 map_string(Process& p,bool unicode) {
    const auto locale=(arg(p,0)==0x400 || arg(p,0)==0x800)?0x409u:arg(p,0),flags=arg(p,1),source=arg(p,2),raw_count=arg(p,3),dest=arg(p,4),capacity=arg(p,5);
    const auto count=(raw_count&0x80000000u)?0xffffffffu:raw_count;
#ifdef _WIN32
    if(unicode){const auto input=wide(p,source,int(std::bit_cast<std::int32_t>(count)));const auto* text=reinterpret_cast<const wchar_t*>(input.data());const auto need=LCMapStringW(locale,flags,text,int(input.size()),nullptr,0);
        if(!need){p.set_error(GetLastError());return 0;}if(!capacity)return U32(need);if(capacity<U32(need)){p.set_error(122);return 0;}
        std::vector<wchar_t> result(std::size_t(need),0);if(!LCMapStringW(locale,flags,text,int(input.size()),result.data(),need)){p.set_error(GetLastError());return 0;}
        if(flags&0x400)p.memory.copy_in(dest,std::span(reinterpret_cast<const std::uint8_t*>(result.data()),std::size_t(need)));
        else{p.memory.check(dest,std::size_t(need)*2,Memory::Write);for(unsigned n=0;n<unsigned(need);++n)p.memory.store(dest+n*2,U32(result[n]),16);}return U32(need);}
    const auto input=narrow(p,source,int(std::bit_cast<std::int32_t>(count)));auto need=LCMapStringA(locale,flags,reinterpret_cast<const char*>(input.data()),int(input.size()),nullptr,0);
    if(!need){p.set_error(GetLastError());return 0;}if(!capacity)return U32(need);if(capacity<U32(need)){p.set_error(122);return 0;}
    std::vector<std::uint8_t> result(std::size_t(need),0);if(!LCMapStringA(locale,flags,reinterpret_cast<const char*>(input.data()),int(input.size()),reinterpret_cast<char*>(result.data()),need)){p.set_error(GetLastError());return 0;}p.memory.copy_in(dest,result);return U32(need);
#else
    if(locale!=0x409 && locale!=0 && locale!=0x7f)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"portable NLS supports only measured en-US case mapping");
    if(flags!=0x100 && flags!=0x200)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"portable NLS sort keys/other transformations not implemented");
    std::u16string input;if(unicode)input=wide(p,source,int(std::bit_cast<std::int32_t>(count)));else for(auto ch:narrow(p,source,int(std::bit_cast<std::int32_t>(count))))input.push_back(ch>=128 && ch<160?cp1252[ch-128]:char16_t(ch));
    std::vector<U32> output;output.reserve(input.size());
    for(auto ch:input){const auto* row=nls_row(ch);if(!row)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"case mapping outside measured portable NLS corpus");U32 mapped=flags==0x100?row->lower:row->upper;
        if(!unicode){auto it=std::find(cp1252.begin(),cp1252.end(),char16_t(mapped));if(it!=cp1252.end())mapped=128u+U32(std::distance(cp1252.begin(),it));else if(mapped>=128 && (mapped<160 || mapped>255))throw GuestFault(FaultKind::unsupported,p.cpu.eip,"case mapping requires unimplemented CP1252 best-fit conversion");}output.push_back(mapped);}
    if(!capacity)return U32(output.size());if(capacity<output.size()){p.set_error(122);return 0;}
    p.memory.check(dest,output.size()*(unicode?2:1),Memory::Write);for(unsigned n=0;n<output.size();++n)p.memory.store(dest+n*(unicode?2:1),output[n],unicode?16:8);return U32(output.size());
#endif
}
#ifdef _WIN32
void find_data(Process& p,U32 destination,const WIN32_FIND_DATAW& data) {
    std::array<std::uint8_t,320> result{};auto word=[&](unsigned off,U32 value){for(unsigned n=0;n<4;++n)result[off+n]=std::uint8_t(value>>(8*n));};
    word(0,data.dwFileAttributes);word(4,data.ftCreationTime.dwLowDateTime);word(8,data.ftCreationTime.dwHighDateTime);
    word(12,data.ftLastAccessTime.dwLowDateTime);word(16,data.ftLastAccessTime.dwHighDateTime);word(20,data.ftLastWriteTime.dwLowDateTime);word(24,data.ftLastWriteTime.dwHighDateTime);
    word(28,data.nFileSizeHigh);word(32,data.nFileSizeLow);word(36,data.dwReserved0);word(40,data.dwReserved1);
    BOOL substituted=FALSE;const int count=::WideCharToMultiByte(1252,0x400,data.cFileName,-1,reinterpret_cast<char*>(result.data()+44),260,nullptr,&substituted);
    if(!count || substituted)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"file enumeration name outside guest CP1252 repertoire");
    if(data.cAlternateFileName[0]){substituted=FALSE;if(!::WideCharToMultiByte(1252,0x400,data.cAlternateFileName,-1,reinterpret_cast<char*>(result.data()+304),14,nullptr,&substituted) || substituted)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"alternate filename cannot be marshalled");}
    p.memory.copy_in(destination,result);
}
#else
bool file_pattern(std::string pattern,std::string name) {
    // The portable profile admits '*' and ASCII '?' matching; Windows itself
    // handles full DOS wildcard rules on the target Windows backend.
    if(pattern=="*.*")pattern="*";
    auto lower=[](std::string& v){for(auto& c:v){if(static_cast<unsigned char>(c)>=128)throw std::runtime_error("portable filename enumeration requires ASCII names");c=char(std::tolower(static_cast<unsigned char>(c)));}};
    lower(pattern);lower(name);std::vector<bool> row(name.size()+1);row[0]=true;
    for(auto c:pattern){std::vector<bool> next(name.size()+1);if(c=='*'){next[0]=row[0];for(std::size_t n=1;n<=name.size();++n)next[n]=row[n]||next[n-1];}
        else for(std::size_t n=1;n<=name.size();++n)next[n]=row[n-1]&&(c=='?' || c==name[n-1]);row.swap(next);}
    return row.back();
}
void find_data(Process& p,U32 destination,const std::filesystem::path& path) {
    struct stat data{};if(::stat(path.c_str(),&data))throw GuestFault(FaultKind::unsupported,p.cpu.eip,"enumerated file changed before metadata could be read");
    const auto name=path.filename().string();if(name.size()>=260)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"enumeration filename too long");
    std::array<std::uint8_t,320> result{};auto word=[&](unsigned off,U32 value){for(unsigned n=0;n<4;++n)result[off+n]=std::uint8_t(value>>(8*n));};
    auto time=[&](unsigned off,std::int64_t seconds,long nanoseconds){const auto t=std::uint64_t(seconds+11644473600ll)*10000000ull+std::uint64_t(nanoseconds/100);word(off,U32(t));word(off+4,U32(t>>32));};
    word(0,S_ISDIR(data.st_mode)?16u:128u);time(12,data.st_atim.tv_sec,data.st_atim.tv_nsec);time(20,data.st_mtim.tv_sec,data.st_mtim.tv_nsec);
    const auto size=S_ISDIR(data.st_mode)?0ull:std::uint64_t(data.st_size);word(28,U32(size>>32));word(32,U32(size));std::copy(name.begin(),name.end(),result.begin()+44);
    // Creation time and 8.3 aliases are unavailable on this portable filesystem.
    p.memory.copy_in(destination,result);
}
#endif
void system_time(Process& p,U32 pointer,bool local) {
    const auto now=std::chrono::system_clock::now();const auto t=std::chrono::system_clock::to_time_t(now);std::tm tm{};
#ifdef _WIN32
    if(local)localtime_s(&tm,&t);else gmtime_s(&tm,&t);
#else
    (void)local;gmtime_r(&t,&tm); // portable guest time zone is explicitly UTC
#endif
    const std::array<unsigned,8> fields{unsigned(tm.tm_year+1900),unsigned(tm.tm_mon+1),unsigned(tm.tm_wday),unsigned(tm.tm_mday),unsigned(tm.tm_hour),unsigned(tm.tm_min),unsigned(tm.tm_sec),unsigned(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()%1000)};
    p.memory.check(pointer,16,Memory::Write);for(unsigned n=0;n<8;++n)p.memory.store(pointer+n*2,fields[n],16);
}
}
std::filesystem::path guest_file_path(Process& p,const std::string& name){return guest_path(p,name);}
LargeHeapCacheStatistics large_heap_cache_statistics(Process& p){return large_heap_cache(p).stats;}
void set_large_heap_cache_enabled(Process& p,bool enabled){large_heap_cache(p).enabled=enabled;}
void install_win32(Process& p) {
    kernel(p,"GetLastError",0,[](auto& q){return q.cpu.fs_valid?q.memory.load(q.cpu.fs_base+0x34,32):q.last_error;});kernel(p,"SetLastError",1,[](auto& q){q.set_error(arg(q,0));return 0;});
    kernel(p,"ExitProcess",1,[](auto& q){q.exit(arg(q,0));return 0;});
    kernel(p,"GetCurrentProcess",0,[](auto&){return 0xffffffffu;});kernel(p,"GetCurrentThread",0,[](auto&){return 0xfffffffeu;});
    kernel(p,"GetCurrentThreadId",0,[](auto& q){return q.state().current_thread_id;});kernel(p,"GetCurrentProcessId",0,[](auto&){return 1;});
    kernel(p,"TerminateProcess",2,[](auto& q){if(arg(q,0)!=0xffffffffu){q.set_error(6);return 0;}q.exit(arg(q,1));return 1;});
    kernel(p,"VirtualAlloc",4,[](auto& q){return q.virtual_alloc(arg(q,0),arg(q,1),arg(q,2),arg(q,3));});
    kernel(p,"VirtualFree",3,[](auto& q){return U32(q.virtual_free(arg(q,0),arg(q,1),arg(q,2)));});
    kernel(p,"VirtualProtect",4,[](auto& q){return U32(q.virtual_protect(arg(q,0),arg(q,1),arg(q,2),arg(q,3)));});
    kernel(p,"HeapCreate",3,[](auto& q){return heap_create(q,arg(q,0),arg(q,1),arg(q,2));});
    kernel(p,"GetProcessHeap",0,[](auto& q){if(!q.state().process_heap)q.state().process_heap=heap_create(q,0,0,0);return q.state().process_heap;});
    kernel(p,"HeapAlloc",3,[](auto& q){return heap_allocate(q,arg(q,0),arg(q,1),arg(q,2));});
    kernel(p,"HeapFree",3,[](auto& q){return heap_free(q,arg(q,0),arg(q,1),arg(q,2));});
    kernel(p,"HeapReAlloc",4,[](auto& q){return heap_reallocate(q,arg(q,0),arg(q,1),arg(q,2),arg(q,3));});
    kernel(p,"HeapSize",3,[](auto& q){auto it=q.state().heaps.find(arg(q,0));if(it==q.state().heaps.end() || arg(q,1)&~1u){q.set_error(87);return 0xffffffffu;}auto b=it->second.blocks.find(arg(q,2));if(b==it->second.blocks.end()){q.set_error(87);return 0xffffffffu;}return b->second.size;});
    kernel(p,"HeapDestroy",1,[](auto& q){const auto heap=arg(q,0);auto it=q.state().heaps.find(heap);if(it==q.state().heaps.end() || heap==q.state().process_heap){q.set_error(6);return 0;}heap_drop_idle(q,it->second);heap_drop_all_large(q,it->second);for(auto& [at,block]:it->second.blocks)if(!block.pool)q.memory.release(at);for(auto& [at,pool]:it->second.pools){(void)pool;q.memory.release(at);}q.state().heaps.erase(it);return 1;});
    kernel(p,"GlobalAlloc",2,[](auto& q){const auto flags=arg(q,0);if(flags&~0x40u)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"movable global handles not implemented");auto at=q.allocate_bytes(arg(q,1));q.state().global_blocks.emplace(at,arg(q,1));return at;});
    kernel(p,"GlobalFree",1,[](auto& q){auto at=arg(q,0);if(!at)return 0u;if(!q.state().global_blocks.erase(at)){q.set_error(6);return at;}q.memory.release(at);return 0u;});
    kernel(p,"GlobalReAlloc",3,[](auto& q){
        const auto at=arg(q,0),size=arg(q,1),flags=arg(q,2);auto& blocks=q.state().global_blocks;
        if(flags&~0x42u)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"GlobalReAlloc attributes/movable handles not implemented");
        auto found=blocks.find(at);if(found==blocks.end()){q.set_error(6);return 0u;}
        const auto old_size=found->second;const auto capacity=q.memory.allocation(at).size;
        if(size<=capacity){if((flags&0x40u) && size>old_size)zero(q.memory,at+old_size,size-old_size);found->second=size;return at;}
        // A fixed block must remain in place unless GMEM_MOVEABLE is requested.
        if(!(flags&2u)){q.set_error(8);return 0u;}
        if(size>512u*1024*1024){q.set_error(8);return 0u;}
        auto moved=q.allocate_bytes(size);std::vector<std::uint8_t> copy(std::min(old_size,size));q.memory.copy_out(at,copy);q.memory.copy_in(moved,copy);
        q.memory.release(at);blocks.erase(found);blocks.emplace(moved,size);return moved;
    });
    kernel(p,"TlsAlloc",0,[](auto& q){for(U32 n=0;n<1088;++n)if(!q.state().tls_used.contains(n)){q.state().tls_used.insert(n);q.state().tls[n]=0;return n;}q.set_error(8);return 0xffffffffu;});
    kernel(p,"TlsGetValue",1,[](auto& q){const auto n=arg(q,0);if(n>=1088){q.set_error(87);return 0u;}q.set_error(0);return q.state().tls.contains(n)?q.state().tls.at(n):0u;});
    kernel(p,"TlsSetValue",2,[](auto& q){const auto n=arg(q,0);if(n>=1088){q.set_error(87);return 0;}q.state().tls[n]=arg(q,1);return 1;});
    kernel(p,"TlsFree",1,[](auto& q){const auto n=arg(q,0);if(!q.state().tls_used.erase(n)){q.set_error(87);return 0;}q.state().tls.erase(n);return 1;});
    kernel(p,"InitializeCriticalSection",1,[](auto& q){auto at=arg(q,0);if(q.state().critical_sections.contains(at))throw GuestFault(FaultKind::unsupported,q.cpu.eip,"critical section initialized twice");q.memory.check(at,24,Memory::Write);zero(q.memory,at,24);q.memory.store(at+4,0xffffffffu,32);q.state().critical_sections.insert(at);q.state().critical_depth[at]=0;return 0;});
    kernel(p,"EnterCriticalSection",1,[](auto& q){auto at=arg(q,0);if(!q.state().critical_sections.contains(at))throw GuestFault(FaultKind::unsupported,q.cpu.eip,"uninitialized critical section");if(q.state().critical_depth[at] && q.state().critical_owner[at]!=q.state().current_thread_id){if(!q.state().wait_critical)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"contended critical section without scheduler");q.state().wait_critical(at);if(q.state().api_retry)return 0;}auto& depth=q.state().critical_depth[at];q.memory.check(at+4,12,Memory::Write);++depth;q.state().critical_owner[at]=q.state().current_thread_id;q.memory.store(at+4,depth-1,32);q.memory.store(at+8,depth,32);q.memory.store(at+12,q.state().current_thread_id,32);return 0;});
    kernel(p,"LeaveCriticalSection",1,[](auto& q){auto at=arg(q,0);if(!q.state().critical_sections.contains(at) || !q.state().critical_depth[at] || q.state().critical_owner[at]!=q.state().current_thread_id)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"unowned critical section");auto& depth=q.state().critical_depth[at];q.memory.check(at+4,12,Memory::Write);--depth;q.memory.store(at+4,depth-1,32);q.memory.store(at+8,depth,32);q.memory.store(at+12,depth?q.state().current_thread_id:0,32);if(!depth)q.state().critical_owner.erase(at);return 0;});
    kernel(p,"DeleteCriticalSection",1,[](auto& q){auto at=arg(q,0);if(!q.state().critical_sections.contains(at) || q.state().critical_depth[at])throw GuestFault(FaultKind::unsupported,q.cpu.eip,"cannot delete uninitialized/owned critical section");q.state().critical_sections.erase(at);q.state().critical_depth.erase(at);q.state().critical_owner.erase(at);return 0;});
    kernel(p,"CreateMutexA",3,[](auto& q){const auto security=arg(q,0),initial=arg(q,1),pointer=arg(q,2);
        if(security)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"mutex security descriptors not implemented");
        const auto name=pointer?q.read_string(pointer):std::string();auto object=std::make_shared<Win32State::Mutex>();object->name=name;
#ifdef _WIN32
        // Frame-bounded smoke tests use an isolated single-instance namespace.
        // A test must not hold or wait on a user's independently running game.
        const auto native_name=q.state().stop_after_presents && pointer?"Local\\WinRecompSmoke-"+std::to_string(::GetCurrentProcessId())+"-"+std::to_string(std::hash<std::string>{}(name)):name;
        const auto h=::CreateMutexA(nullptr,initial!=0,pointer?native_name.c_str():nullptr);if(!h){q.set_error(::GetLastError());return 0u;}
        const auto error=::GetLastError();object->native=reinterpret_cast<std::uintptr_t>(h);object->close=[h]{::CloseHandle(h);};q.set_error(error);
#else
        // Explicit single-guest-process namespace. Cross-process named mutexes
        // are implemented by the Windows backend, not claimed on portable hosts.
        auto prior=name.empty()?std::shared_ptr<Win32State::Mutex>{}:q.state().mutex_names[name].lock();
        if(prior){object=prior;q.set_error(183);}else{object->depth=initial?1:0;if(!name.empty())q.state().mutex_names[name]=object;q.set_error(0);}
#endif
        const auto handle=q.state().next_handle;q.state().next_handle+=16;q.state().mutexes[handle]=object;return handle;});
    kernel(p,"ReleaseMutex",1,[](auto& q){auto it=q.state().mutexes.find(arg(q,0));if(it==q.state().mutexes.end()){q.set_error(6);return 0;}
#ifdef _WIN32
        if(!::ReleaseMutex(reinterpret_cast<HANDLE>(it->second->native))){q.set_error(::GetLastError());return 0;}
#else
        if(!it->second->depth){q.set_error(288);return 0;}--it->second->depth;
#endif
        return 1;});
    kernel(p,"WaitForSingleObject",2,[](auto& q){if(q.state().wait_objects){const std::array<U32,1> handles{arg(q,0)};return q.state().wait_objects(handles,false,arg(q,1));}auto it=q.state().mutexes.find(arg(q,0));if(it==q.state().mutexes.end()){q.set_error(6);return 0xffffffffu;}
#ifdef _WIN32
        if(arg(q,1)>1000)throw GuestFault(FaultKind::budget,q.cpu.eip,"host wait exceeds the runtime's 1-second API wait bound");
        auto result=::WaitForSingleObject(reinterpret_cast<HANDLE>(it->second->native),arg(q,1));if(result==WAIT_FAILED)q.set_error(::GetLastError());return U32(result);
#else
        ++it->second->depth;return 0u;
#endif
    });
    kernel(p,"InterlockedIncrement",1,[](auto& q){auto g=q.memory.lock();auto at=arg(q,0);auto v=q.memory.load(at,32)+1;q.memory.store(at,v,32);return v;});
    kernel(p,"InterlockedDecrement",1,[](auto& q){auto g=q.memory.lock();auto at=arg(q,0);auto v=q.memory.load(at,32)-1;q.memory.store(at,v,32);return v;});
    kernel(p,"InterlockedExchange",2,[](auto& q){auto g=q.memory.lock();auto at=arg(q,0);auto old=q.memory.load(at,32);q.memory.store(at,arg(q,1),32);return old;});
    kernel(p,"GetCommandLineA",0,[](auto& q){if(!q.state().command_line)q.state().command_line=q.put_string(q.options.command_line);return q.state().command_line;});
    kernel(p,"GetCommandLineW",0,[](auto& q){if(!q.state().wide_command_line){std::u16string s;for(unsigned char ch:q.options.command_line)s+=char16_t(ch);q.state().wide_command_line=q.put_wstring(s);}return q.state().wide_command_line;});
    auto environment=[](Process& q,bool unicode){std::string s;for(const auto& [key,value]:q.state().environment){s+=key+'='+value;s+=char(0);}s+=char(0);if(s.size()==1)s+=char(0);U32 at;
        if(unicode){std::u16string w;for(unsigned char ch:s)w+=char16_t(ch);at=q.put_wstring(w);}else at=q.put_string(s);q.state().environment_blocks[at]=unicode?2:1;return at;};
    kernel(p,"GetEnvironmentStrings",0,[environment](auto& q){return environment(q,false);});kernel(p,"GetEnvironmentStringsA",0,[environment](auto& q){return environment(q,false);});kernel(p,"GetEnvironmentStringsW",0,[environment](auto& q){return environment(q,true);});
    for(bool unicode:{false,true})kernel(p,unicode?"FreeEnvironmentStringsW":"FreeEnvironmentStringsA",1,[unicode](auto& q){auto at=arg(q,0);auto it=q.state().environment_blocks.find(at);if(it==q.state().environment_blocks.end() || it->second!=(unicode?2u:1u)){q.set_error(87);return 0;}q.memory.release(at);q.state().environment_blocks.erase(it);return 1;});
    kernel(p,"SetEnvironmentVariableA",2,[](auto& q){auto name=q.read_string(arg(q,0));if(name.empty() || name.find('=')!=std::string::npos){q.set_error(87);return 0;}auto value=arg(q,1);if(value)q.state().environment[name]=q.read_string(value);else q.state().environment.erase(name);return 1;});
    kernel(p,"IsProcessorFeaturePresent",1,[](auto& q){
        const auto feature=arg(q,0);
        // The runtime uses native x64 x87/SSE, not an emulated processor or a
        // pre-x64 Pentium with the FDIV erratum. Do not advertise other guest
        // capabilities without a matching decoder/emitter/runtime contract.
        if(feature>1)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"processor feature query not modelled: "+std::to_string(feature));
#ifdef _WIN32
        return U32(::IsProcessorFeaturePresent(feature));
#else
        return 0u;
#endif
    });
    kernel(p,"GetModuleHandleA",1,[](auto& q){auto at=arg(q,0);return at?q.module(q.read_string(at)):q.image_base;});
    kernel(p,"LoadLibraryA",1,[](auto& q){
        const auto name=q.read_string(arg(q,0));if(const auto result=q.module(name,false))return result;
        // A missing optional module is a documented API failure, not an
        // unsupported instruction. Existing untranslated DLLs remain explicit
        // boundaries; they cannot be executed by the native host loader.
        auto path=guest_path(q,name);if(!path.has_extension())path+=".dll";
        if(std::filesystem::exists(path))throw GuestFault(FaultKind::unsupported,q.cpu.eip,"module loading not implemented for "+name);
        q.set_error(126);return 0u;
    });
    kernel(p,"GetProcAddress",2,[](auto& q){const auto name=q.module_name(arg(q,0));if(name.empty()){q.set_error(6);return 0u;}auto pointer=arg(q,1);auto symbol=pointer<0x10000?"#"+std::to_string(pointer):q.read_string(pointer);auto target=q.resolve(name,symbol);if(!target)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"unmodelled dynamic export "+name+"!"+symbol);return target;});
    kernel(p,"GetModuleFileNameA",3,[](auto& q){if(arg(q,0) && arg(q,0)!=q.image_base){q.set_error(6);return 0u;}auto count=arg(q,2),dest=arg(q,1);if(!count){q.set_error(122);return 0u;}std::string name="C:\\WinRecomp\\"+q.options.image_name;auto n=std::min<std::size_t>(name.size(),count-1);q.memory.check(dest,n+1,Memory::Write);q.memory.copy_in(dest,std::span(reinterpret_cast<const std::uint8_t*>(name.data()),n));q.memory.store(dest+U32(n),0,8);if(n<name.size()){q.set_error(122);return count;}return U32(n);});
    kernel(p,"GetStartupInfoA",1,[](auto& q){
        const auto at=arg(q,0);zero(q.memory,at,68);q.memory.store(at,68,32);
#ifdef _WIN32
        // The original CRT uses STARTF_USESHOWWINDOW to choose WinMain's
        // nCmdShow. Dropping it activates a window launched with
        // SW_SHOWNOACTIVATE and makes foreground/background tests differ.
        // Copy scalar fields individually; native pointers/handles must never
        // be copied into the 32-bit guest STARTUPINFOA layout.
        STARTUPINFOA native{};::GetStartupInfoA(&native);
        const std::array<DWORD,8> scalars{native.dwX,native.dwY,native.dwXSize,native.dwYSize,
            native.dwXCountChars,native.dwYCountChars,native.dwFillAttribute,native.dwFlags};
        for(unsigned n=0;n<scalars.size();++n)q.memory.store(at+16+4*n,scalars[n],32);
        q.memory.store(at+48,native.wShowWindow,16);
#endif
        q.memory.store(at+56,q.state().stdin_handle,32);q.memory.store(at+60,q.state().stdout_handle,32);q.memory.store(at+64,q.state().stderr_handle,32);return 0;
    });
    // The portable guest identity is Windows 2000 (5.0), not the host OS.
    kernel(p,"GetVersion",0,[](auto&){return 0x08930005u;});
    kernel(p,"GetVersionExA",1,[](auto& q){auto at=arg(q,0),size=q.memory.load(at,32);if(size!=148 && size!=156){q.set_error(87);return 0;}q.memory.check(at,size,Memory::Write);zero(q.memory,at+4,size-4);q.memory.store(at+4,5,32);q.memory.store(at+8,0,32);q.memory.store(at+12,2195,32);q.memory.store(at+16,2,32);return 1;});
    kernel(p,"GetACP",0,[](auto&){return 1252;});kernel(p,"GetOEMCP",0,[](auto&){return 437;});
    kernel(p,"GetCPInfo",2,[](auto& q){auto cp=arg(q,0),at=arg(q,1);
#ifdef _WIN32
        CPINFO info{};if(!::GetCPInfo(cp,&info)){q.set_error(GetLastError());return 0;}q.memory.check(at,20,Memory::Write);q.memory.store(at,info.MaxCharSize,32);q.memory.copy_in(at+4,std::span(info.DefaultChar,2));q.memory.copy_in(at+6,std::span(info.LeadByte,12));zero(q.memory,at+18,2);return 1;
#else
        if(cp!=0 && cp!=1252 && cp!=437){q.set_error(87);return 0;}zero(q.memory,at,20);q.memory.store(at,1,32);q.memory.store(at+4,'?',8);return 1;
#endif
    });
    kernel(p,"MultiByteToWideChar",6,multibyte_to_wide);kernel(p,"WideCharToMultiByte",8,wide_to_multibyte);
    kernel(p,"GetStringTypeW",4,[](auto& q){return classify(q,true);});kernel(p,"GetStringTypeA",5,[](auto& q){return classify(q,false);});
    kernel(p,"LCMapStringW",6,[](auto& q){return map_string(q,true);});kernel(p,"LCMapStringA",6,[](auto& q){return map_string(q,false);});
    kernel(p,"GetTickCount",0,[](auto& q){return U32(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-q.state().clock_start).count());});
    kernel(p,"GetSystemTime",1,[](auto& q){system_time(q,arg(q,0),false);return 0;});kernel(p,"GetLocalTime",1,[](auto& q){system_time(q,arg(q,0),true);return 0;});
    kernel(p,"GetTimeZoneInformation",1,[](auto& q){auto out=arg(q,0);q.memory.check(out,172,Memory::Write);
#ifdef _WIN32
        TIME_ZONE_INFORMATION zone{};static_assert(sizeof(zone)==172);auto result=::GetTimeZoneInformation(&zone);if(result==TIME_ZONE_ID_INVALID){q.set_error(::GetLastError());return 0xffffffffu;}q.memory.copy_in(out,std::span(reinterpret_cast<const std::uint8_t*>(&zone),sizeof(zone)));return U32(result);
#else
        zero(q.memory,out,172);for(unsigned n=0;n<3;++n){q.memory.store(out+4+n*2,U32("UTC"[n]),16);q.memory.store(out+88+n*2,U32("UTC"[n]),16);}return 0u;
#endif
    });
    kernel(p,"FileTimeToSystemTime",2,[](auto& q){auto input=arg(q,0),output=arg(q,1);const auto low=q.memory.load(input,32),high=q.memory.load(input+4,32);
#ifdef _WIN32
        FILETIME ft{low,high};SYSTEMTIME st{};if(!::FileTimeToSystemTime(&ft,&st)){q.set_error(::GetLastError());return 0;}q.memory.copy_in(output,std::span(reinterpret_cast<const std::uint8_t*>(&st),sizeof(st)));
#else
        const auto ticks=(std::uint64_t(high)<<32)|low;if(ticks>0x7fffffffffffffffull){q.set_error(87);return 0;}
        const auto seconds=std::int64_t(ticks/10000000ull)-11644473600ll;const time_t stamp=time_t(seconds);std::tm tm{};if(!gmtime_r(&stamp,&tm)){q.set_error(87);return 0;}
        const std::array<unsigned,8> fields{unsigned(tm.tm_year+1900),unsigned(tm.tm_mon+1),unsigned(tm.tm_wday),unsigned(tm.tm_mday),unsigned(tm.tm_hour),unsigned(tm.tm_min),unsigned(tm.tm_sec),unsigned((ticks%10000000ull)/10000)};
        q.memory.check(output,16,Memory::Write);for(unsigned n=0;n<8;++n)q.memory.store(output+n*2,fields[n],16);
#endif
        return 1;});
    kernel(p,"FileTimeToLocalFileTime",2,[](auto& q){auto input=arg(q,0),output=arg(q,1);U32 low=q.memory.load(input,32),high=q.memory.load(input+4,32);
#ifdef _WIN32
        FILETIME src{low,high},dest{};if(!::FileTimeToLocalFileTime(&src,&dest)){q.set_error(::GetLastError());return 0;}low=dest.dwLowDateTime;high=dest.dwHighDateTime;
#endif
        q.memory.check(output,8,Memory::Write);q.memory.store(output,low,32);q.memory.store(output+4,high,32);return 1;});
    // Only explicit file handles cross the ABI; a FILE*/HANDLE is never exposed.
    auto add_file=[&](std::FILE* stream,bool rd,bool wr,U32 type){auto id=p.state().next_handle;p.state().next_handle+=16;p.state().files[id]={stream,false,rd,wr,type};return id;};
    p.state().stdin_handle=add_file(stdin,true,false,stream_type(stdin));p.state().stdout_handle=add_file(stdout,false,true,stream_type(stdout));p.state().stderr_handle=add_file(stderr,false,true,stream_type(stderr));
    kernel(p,"GetStdHandle",1,[](auto& q){switch(arg(q,0)){case 0xfffffff6:return q.state().stdin_handle;case 0xfffffff5:return q.state().stdout_handle;case 0xfffffff4:return q.state().stderr_handle;default:q.set_error(87);return 0xffffffffu;}});
    kernel(p,"SetStdHandle",2,[](auto& q){auto at=arg(q,1);switch(arg(q,0)){case 0xfffffff6:q.state().stdin_handle=at;return 1;case 0xfffffff5:q.state().stdout_handle=at;return 1;case 0xfffffff4:q.state().stderr_handle=at;return 1;default:q.set_error(87);return 0;}});
    kernel(p,"GetFileType",1,[](auto& q){auto it=q.state().files.find(arg(q,0));if(it==q.state().files.end()){q.set_error(6);return 0u;}return it->second.type;});
    kernel(p,"SetHandleCount",1,[](auto& q){return arg(q,0);}); // obsolete Win32 API returns its input
    kernel(p,"CloseHandle",1,[](auto& q){auto it=q.state().files.find(arg(q,0));if(it!=q.state().files.end()){if(it->second.owned && it->second.stream)std::fclose(it->second.stream);q.state().files.erase(it);return 1;}if(q.state().mutexes.erase(arg(q,0)))return 1;if(q.state().events.erase(arg(q,0)))return 1;q.set_error(6);return 0;});
    kernel(p,"CreateFileA",7,[](auto& q){const auto name=q.read_string(arg(q,0));const auto path=guest_path(q,name);auto access=arg(q,1),share=arg(q,2),security=arg(q,3),creation=arg(q,4),flags=arg(q,5),templ=arg(q,6);
        if(security || templ || flags&~0x80u || share&~7u || access&~0xc0000000u)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"unsupported CreateFileA flags/security/template");
        const bool write=(access&0x40000000u)!=0,read=(access&0x80000000u)!=0;
        if(!read && !write)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"metadata-only CreateFileA not implemented");
        if((write || creation!=3) && !q.options.allow_file_write){q.set_error(5);return 0xffffffffu;}
        const auto mounted=mounted_info(q,path);if(mounted && ((mounted->attributes&16) || write || creation!=3)){q.set_error(5);return 0xffffffffu;}
        std::error_code ec;bool exists=mounted.has_value() || std::filesystem::exists(path,ec);if(ec){q.set_error(3);return 0xffffffffu;}
        if(creation<1 || creation>5){q.set_error(87);return 0xffffffffu;}if(creation==1 && exists){q.set_error(80);return 0xffffffffu;}
        if((creation==3 || creation==5) && !exists){q.set_error(2);return 0xffffffffu;}if(creation==5 && !write){q.set_error(5);return 0xffffffffu;}
        // Native Windows enforces cross-process sharing. The portable process
        // also enforces both directions of sharing against its open handles.
        for(const auto& [id,old]:q.state().files){(void)id;if(old.path==path &&
            ((read && !(old.share&1)) || (write && !(old.share&2)) || (old.read && !(share&1)) || (old.write && !(share&2)))){q.set_error(32);return 0xffffffffu;}}
        std::FILE* file=nullptr;U32 open_error_code{};
        if(mounted){file=q.state().read_only_files->open(mounted_name(q,path));if(!file){q.set_error(file_error(errno));return 0xffffffffu;}}
        else {
#ifdef _WIN32
        HANDLE h=::CreateFileW(path.c_str(),access,share,nullptr,creation,flags?flags:FILE_ATTRIBUTE_NORMAL,nullptr);
        if(h==INVALID_HANDLE_VALUE){q.set_error(::GetLastError());return 0xffffffffu;}
        open_error_code=::GetLastError();const int fd=_open_osfhandle(reinterpret_cast<intptr_t>(h),_O_BINARY|(read?(write?_O_RDWR:_O_RDONLY):_O_WRONLY));
        if(fd<0){::CloseHandle(h);q.set_error(file_error(errno));return 0xffffffffu;}
        file=_fdopen(fd,read?(write?"r+b":"rb"):"wb");if(!file){_close(fd);q.set_error(file_error(errno));return 0xffffffffu;}
#else
        int mode=read?(write?O_RDWR:O_RDONLY):O_WRONLY;
        if(creation==1)mode|=O_CREAT|O_EXCL;else if(creation==2)mode|=O_CREAT|O_TRUNC;else if(creation==4)mode|=O_CREAT;else if(creation==5)mode|=O_TRUNC;
        const int fd=::open(path.c_str(),mode|O_CLOEXEC,0600);if(fd<0){q.set_error(file_error(errno));return 0xffffffffu;}
        file=fdopen(fd,read?(write?"r+b":"rb"):"wb");if(!file){::close(fd);q.set_error(file_error(errno));return 0xffffffffu;}
#endif
        }
        std::setvbuf(file,nullptr,_IONBF,0);
        auto id=q.state().next_handle;q.state().next_handle+=16;q.state().files[id]={file,true,read,write,1,path,share};
        if(creation==2 || creation==4){
#ifdef _WIN32
            q.set_error(open_error_code);
#else
            q.set_error(exists?183:0);
#endif
        }return id;});
    for(bool write:{false,true})kernel(p,write?"WriteFile":"ReadFile",5,[write](auto& q){auto it=q.state().files.find(arg(q,0));if(it==q.state().files.end()){q.set_error(6);return 0;}auto pointer=arg(q,1),size=arg(q,2),transferred=arg(q,3),overlap=arg(q,4);
        if(overlap)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"overlapped file I/O not implemented");if(!transferred)throw GuestFault(FaultKind::memory,0,"synchronous file I/O requires byte-count pointer");
        if(size>64u*1024*1024)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"file I/O request exceeds safety bound");q.memory.check(transferred,4,Memory::Write);q.memory.check(pointer,size,write?Memory::Read:Memory::Write);
        auto& f=it->second;if((write && !f.write) || (!write && !f.read)){q.set_error(5);q.memory.store(transferred,0,32);return 0;}
        std::vector<std::uint8_t> data(size);if(write)q.memory.copy_out(pointer,data);U32 done=0;bool success;
#ifdef _WIN32
        DWORD count=0;const auto h=file_handle(f);success=(write?::WriteFile(h,data.data(),size,&count,nullptr) : ::ReadFile(h,data.data(),size,&count,nullptr))!=FALSE;
        done=count;if(!success)q.set_error(::GetLastError());
#else
        ssize_t count;do{count=write?::write(fileno(f.stream),data.data(),size) : ::read(fileno(f.stream),data.data(),size);}while(count<0 && errno==EINTR);
        success=count>=0;if(success)done=U32(count);else q.set_error(file_error(errno));
#endif
        if(!write && done)q.memory.copy_in(pointer,std::span(data.data(),done));q.memory.store(transferred,done,32);return success?1:0;});
    kernel(p,"FlushFileBuffers",1,[](auto& q){auto it=q.state().files.find(arg(q,0));if(it==q.state().files.end()){q.set_error(6);return 0;}
#ifdef _WIN32
        if(!::FlushFileBuffers(file_handle(it->second))){q.set_error(::GetLastError());return 0;}
#else
        if(!it->second.write){q.set_error(5);return 0;}if(::fsync(fileno(it->second.stream))){q.set_error(file_error(errno));return 0;}
#endif
        return 1;});

    kernel(p,"GetFileSize",2,[](auto& q){auto it=q.state().files.find(arg(q,0));if(it==q.state().files.end()){q.set_error(6);return 0xffffffffu;}auto high=arg(q,1);if(high)q.memory.check(high,4,Memory::Write);
#ifdef _WIN32
        LARGE_INTEGER info{};if(!::GetFileSizeEx(file_handle(it->second),&info)){q.set_error(::GetLastError());return 0xffffffffu;}
        const auto length=info.QuadPart;
#else
        struct stat info{};if(fstat(fileno(it->second.stream),&info)){q.set_error(file_error(errno));return 0xffffffffu;}
        const auto length=info.st_size;
#endif
        if(length<0){q.set_error(87);return 0xffffffffu;}auto size=std::uint64_t(length);if(high)q.memory.store(high,U32(size>>32),32);if(U32(size)==0xffffffffu)q.set_error(0);return U32(size);});
    kernel(p,"SetFilePointer",4,[](auto& q){auto it=q.state().files.find(arg(q,0));if(it==q.state().files.end()){q.set_error(6);return 0xffffffffu;}auto high=arg(q,2),method=arg(q,3);if(method>2){q.set_error(87);return 0xffffffffu;}std::int64_t distance;
        if(high){q.memory.check(high,4,Memory::Read|Memory::Write);distance=std::bit_cast<std::int64_t>((std::uint64_t(q.memory.load(high,32))<<32)|arg(q,1));}
        else distance=std::bit_cast<std::int32_t>(arg(q,1));
        const auto origin=method==0?SEEK_SET:method==1?SEEK_CUR:SEEK_END;
#ifdef _WIN32
        LARGE_INTEGER move{},where{};move.QuadPart=distance;
        if(!::SetFilePointerEx(file_handle(it->second),move,&where,method)){q.set_error(::GetLastError());return 0xffffffffu;}const auto position=where.QuadPart;
#else
        static_assert(sizeof(off_t)>=8);const auto position=::lseek(fileno(it->second.stream),off_t(distance),origin);if(position<0){q.set_error(errno==EINVAL?131:file_error(errno));return 0xffffffffu;}
#endif
        if(position<0){q.set_error(6);return 0xffffffffu;}if(high)q.memory.store(high,U32(std::uint64_t(position)>>32),32);if(U32(position)==0xffffffffu)q.set_error(0);return U32(position);});
    kernel(p,"SetEndOfFile",1,[](auto& q){auto it=q.state().files.find(arg(q,0));if(it==q.state().files.end()){q.set_error(6);return 0;}auto& f=it->second;if(!f.write || !q.options.allow_file_write){q.set_error(5);return 0;}
        if(std::fflush(f.stream)){q.set_error(5);return 0;}
#ifdef _WIN32
        if(!::SetEndOfFile(file_handle(f))){q.set_error(::GetLastError());return 0;}
#else
        const auto position=::lseek(fileno(f.stream),0,SEEK_CUR);if(position<0 || ::ftruncate(fileno(f.stream),position)){q.set_error(file_error(errno));return 0;}
#endif
        return 1;});
    kernel(p,"FindFirstFileA",2,[](auto& q){const auto name=q.read_string(arg(q,0));auto path=guest_path(q,name);const auto out=arg(q,1);q.memory.check(out,320,Memory::Write);
        auto state=std::make_shared<Win32State::Find>();
        const auto mounted=mounted_info(q,path.parent_path());
        if(mounted && (mounted->attributes&16)){
            const auto pattern=path.filename().string();state->mounted=true;
            for(auto& item:q.state().read_only_files->list(mounted_name(q,path.parent_path())))if(mounted_pattern(pattern,item.name))state->mounted_entries.push_back(std::move(item));
            std::error_code ec;auto directory=std::filesystem::directory_iterator(path.parent_path(),ec);
            if(ec && ec!=std::errc::no_such_file_or_directory){q.set_error(file_error(ec.value()));return 0xffffffffu;}
            if(!ec)for(const auto& entry:directory){const auto filename=entry.path().filename().string();if(!mounted_pattern(pattern,filename) || mounted_info(q,entry.path()))continue;
                ReadOnlyFileInfo item{filename,0,0,entry.is_directory()?16u:128u};if(!(item.attributes&16))item.size=entry.file_size();state->mounted_entries.push_back(std::move(item));}
            std::sort(state->mounted_entries.begin(),state->mounted_entries.end(),[](const auto& a,const auto& b){return a.name<b.name;});
            if(state->mounted_entries.empty()){q.set_error(2);return 0xffffffffu;}mounted_find_data(q,out,state->mounted_entries.front());state->next=1;
            const auto id=q.state().next_handle;q.state().next_handle+=16;q.state().finds[id]=state;return id;
        }
#ifdef _WIN32
        WIN32_FIND_DATAW data{};const auto handle=::FindFirstFileW(path.c_str(),&data);if(handle==INVALID_HANDLE_VALUE){q.set_error(::GetLastError());return 0xffffffffu;}
        state->native=reinterpret_cast<std::uintptr_t>(handle);state->close=[handle]{::FindClose(handle);};find_data(q,out,data);
#else
        auto parent=path.parent_path();const auto pattern=path.filename().string();if(parent.string().find_first_of("*?")!=std::string::npos)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"wildcards in directory components are not supported");
        std::error_code ec;auto iter=std::filesystem::directory_iterator(parent,ec);if(ec){q.set_error(file_error(ec.value()));return 0xffffffffu;}
        for(const auto& item:iter){if(file_pattern(pattern,item.path().filename().string()))state->entries.push_back(item.path());}
        std::sort(state->entries.begin(),state->entries.end());if(state->entries.empty()){q.set_error(2);return 0xffffffffu;}
        find_data(q,out,state->entries.front());state->next=1;
#endif
        const auto id=q.state().next_handle;q.state().next_handle+=16;q.state().finds[id]=state;return id;});
    kernel(p,"FindNextFileA",2,[](auto& q){const auto it=q.state().finds.find(arg(q,0));if(it==q.state().finds.end()){q.set_error(6);return 0;}auto out=arg(q,1);q.memory.check(out,320,Memory::Write);
        if(it->second->mounted){auto& find=*it->second;if(find.next>=find.mounted_entries.size()){q.set_error(18);return 0;}mounted_find_data(q,out,find.mounted_entries[find.next++]);return 1;}
#ifdef _WIN32
        WIN32_FIND_DATAW data{};if(!::FindNextFileW(reinterpret_cast<HANDLE>(it->second->native),&data)){q.set_error(::GetLastError());return 0;}find_data(q,out,data);
#else
        auto& find=*it->second;if(find.next>=find.entries.size()){q.set_error(18);return 0;}find_data(q,out,find.entries.at(find.next++));
#endif
        return 1;});
    kernel(p,"FindClose",1,[](auto& q){if(!q.state().finds.erase(arg(q,0))){q.set_error(6);return 0;}return 1;});
    kernel(p,"GetFileInformationByHandle",2,[](auto& q){auto it=q.state().files.find(arg(q,0));if(it==q.state().files.end()){q.set_error(6);return 0;}auto dest=arg(q,1);q.memory.check(dest,52,Memory::Write);std::array<U32,13> words{};
#ifdef _WIN32
        BY_HANDLE_FILE_INFORMATION data{};if(!::GetFileInformationByHandle(file_handle(it->second),&data)){q.set_error(::GetLastError());return 0;}
        words={data.dwFileAttributes,data.ftCreationTime.dwLowDateTime,data.ftCreationTime.dwHighDateTime,data.ftLastAccessTime.dwLowDateTime,data.ftLastAccessTime.dwHighDateTime,data.ftLastWriteTime.dwLowDateTime,data.ftLastWriteTime.dwHighDateTime,data.dwVolumeSerialNumber,data.nFileSizeHigh,data.nFileSizeLow,data.nNumberOfLinks,data.nFileIndexHigh,data.nFileIndexLow};
#else
        struct stat st{};if(::fstat(fileno(it->second.stream),&st)){q.set_error(file_error(errno));return 0;}
        auto stamp=[](std::int64_t seconds,long ns){return std::uint64_t(seconds+11644473600ll)*10000000ull+std::uint64_t(ns/100);};auto atime=stamp(st.st_atim.tv_sec,st.st_atim.tv_nsec),mtime=stamp(st.st_mtim.tv_sec,st.st_mtim.tv_nsec);
        words={S_ISDIR(st.st_mode)?16u:128u,0,0,U32(atime),U32(atime>>32),U32(mtime),U32(mtime>>32),U32(st.st_dev),U32(std::uint64_t(st.st_size)>>32),U32(st.st_size),U32(st.st_nlink),U32(std::uint64_t(st.st_ino)>>32),U32(st.st_ino)};
#endif
        for(unsigned n=0;n<words.size();++n)q.memory.store(dest+n*4,words[n],32);return 1;});
    kernel(p,"CreateDirectoryA",2,[](auto& q){auto path=guest_path(q,q.read_string(arg(q,0)));if(arg(q,1))throw GuestFault(FaultKind::unsupported,q.cpu.eip,"directory security descriptors not implemented");
        if(!q.options.allow_file_write){q.set_error(5);return 0;}
#ifdef _WIN32
        if(!::CreateDirectoryW(path.c_str(),nullptr)){q.set_error(::GetLastError());return 0;}
#else
        std::error_code ec;const bool made=std::filesystem::create_directory(path,ec);if(!made){q.set_error(ec?file_error(ec.value()):183);return 0;}
#endif
        return 1;});
    kernel(p,"RemoveDirectoryA",1,[](auto& q){auto path=guest_path(q,q.read_string(arg(q,0)));if(!q.options.allow_file_write || path==q.options.data_root){q.set_error(5);return 0;}
        if(mounted_info(q,path)){q.set_error(5);return 0;}
#ifdef _WIN32
        if(!::RemoveDirectoryW(path.c_str())){q.set_error(::GetLastError());return 0;}
#else
        if(::rmdir(path.c_str())){q.set_error(errno==ENOTEMPTY?145:file_error(errno));return 0;}
#endif
        return 1;});
    kernel(p,"DeleteFileA",1,[](auto& q){auto path=guest_path(q,q.read_string(arg(q,0)));if(!q.options.allow_file_write || path==q.options.data_root){q.set_error(5);return 0;}
        if(mounted_info(q,path)){q.set_error(5);return 0;}
        for(const auto& [id,old]:q.state().files){(void)id;if(old.path==path && !(old.share&4)){q.set_error(32);return 0;}}
#ifdef _WIN32
        if(!::DeleteFileW(path.c_str())){q.set_error(::GetLastError());return 0;}
#else
        if(::unlink(path.c_str())){q.set_error(file_error(errno));return 0;}
#endif
        return 1;});
    kernel(p,"GetCurrentDirectoryA",2,[](auto& q){std::string name;if(q.state().current_directory.find(':' )!=std::string::npos)name=q.state().current_directory;else{name="C:\\WinRecomp";if(!q.state().current_directory.empty()){name+='\\';name+=q.state().current_directory;}}std::replace(name.begin(),name.end(),'/','\\');auto size=arg(q,0),at=arg(q,1);if(size<=name.size())return U32(name.size()+1);q.memory.copy_in(at,std::span(reinterpret_cast<const std::uint8_t*>(name.c_str()),name.size()+1));return U32(name.size());});
    kernel(p,"GetFileAttributesA",1,[](auto& q){const auto path=guest_path(q,q.read_string(arg(q,0)));
        if(const auto info=mounted_info(q,path))return info->attributes;
#ifdef _WIN32
        const auto attrs=::GetFileAttributesW(path.c_str());if(attrs==INVALID_FILE_ATTRIBUTES)q.set_error(::GetLastError());return U32(attrs);
#else
        struct stat st{};if(::stat(path.c_str(),&st)){q.set_error(file_error(errno));return 0xffffffffu;}
        U32 attrs=S_ISDIR(st.st_mode)?0x10u:0u;if(!(st.st_mode&0222))attrs|=1;return attrs?attrs:0x80u;
#endif
    });
    kernel(p,"SetFileAttributesA",2,[](auto& q){auto path=guest_path(q,q.read_string(arg(q,0)));const auto flags=arg(q,1);
        if(mounted_info(q,path)){q.set_error(5);return 0;}
        if(!q.options.allow_file_write || path==q.options.data_root){q.set_error(5);return 0;}
#ifdef _WIN32
        if(!::SetFileAttributesW(path.c_str(),flags)){q.set_error(::GetLastError());return 0;}
#else
        if(flags&~0x81u)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"portable file attributes support READONLY/NORMAL only");
        struct stat st{};if(::stat(path.c_str(),&st)){q.set_error(file_error(errno));return 0;}
        const auto mode=(flags&1)?st.st_mode&~0222u:st.st_mode|0200u;if(::chmod(path.c_str(),mode)){q.set_error(file_error(errno));return 0;}
#endif
        return 1;});
    kernel(p,"MoveFileA",2,[](auto& q){auto from=guest_path(q,q.read_string(arg(q,0))),to=guest_path(q,q.read_string(arg(q,1)));
        if(!q.options.allow_file_write || from==q.options.data_root || to==q.options.data_root){q.set_error(5);return 0;}
        for(const auto& [id,file]:q.state().files){(void)id;if((file.path==from || file.path==to) && !(file.share&4)){q.set_error(32);return 0;}}
#ifdef _WIN32
        if(!::MoveFileW(from.c_str(),to.c_str())){q.set_error(::GetLastError());return 0;}
#else
        // RENAME_NOREPLACE preserves Windows' no-clobber contract atomically.
        if(::syscall(SYS_renameat2,AT_FDCWD,from.c_str(),AT_FDCWD,to.c_str(),1)){
            if(errno==ENOSYS || errno==EXDEV)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"portable atomic MoveFile is unavailable for this filesystem");
            q.set_error(errno==EEXIST?183:file_error(errno));return 0;
        }
#endif
        return 1;});
    // The guest sees one virtual drive, rooted at --root. Do not leak unrelated
    // host drives or silently treat guest C:\\ as the real host filesystem root.
    kernel(p,"GetLogicalDrives",0,[](auto&){return 4u;});
    kernel(p,"GetDriveTypeA",1,[](auto& q){auto ptr=arg(q,0);auto name=ptr?q.read_string(ptr):"C:\\";for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
        if(name!="c:\\" && name!="c:/")return 1u;
#ifdef _WIN32
        return U32(::GetDriveTypeW(q.options.data_root.root_path().c_str()));
#else
        return 3u; // explicit portable guest profile: one fixed drive
#endif
    });
    kernel(p,"GetVolumeInformationA",8,[](auto& q){auto pointer=arg(q,0);auto drive=pointer?q.read_string(pointer):"C:\\";for(auto& c:drive)c=char(std::tolower(static_cast<unsigned char>(c)));
        if(drive!="c:\\" && drive!="c:/"){q.set_error(3);return 0;}
        const auto label=arg(q,1),label_size=arg(q,2),serial=arg(q,3),length=arg(q,4),flags=arg(q,5),fs=arg(q,6),fs_size=arg(q,7);
        std::string volume="WinRecomp",filesystem="POSIX";U32 number=0x57524350u,max_component=255,attributes=3;
#ifdef _WIN32
        // GetVolumeInformation limits BOTH buffers to MAX_PATH+1 characters.
        // Passing 32768 was outside that contract and triggered a heap overflow
        // inside the Windows ANSI wrapper before a later, unrelated heap free.
        // Host buffer bounds are fixed here, never copied from guest capacities.
        char native_label[MAX_PATH+1]{},native_fs[MAX_PATH+1]{};DWORD native_serial{},native_max{},native_flags{};
        auto hostroot=q.options.data_root.root_path().string();if(!::GetVolumeInformationA(hostroot.c_str(),native_label,sizeof(native_label),&native_serial,&native_max,&native_flags,native_fs,sizeof(native_fs))){q.set_error(::GetLastError());return 0;}
        volume=native_label;filesystem=native_fs;number=native_serial;max_component=native_max;attributes=native_flags;
#else
        struct statvfs info{};if(::statvfs(q.options.data_root.c_str(),&info)){q.set_error(file_error(errno));return 0;}max_component=U32(std::min<unsigned long>(info.f_namemax,0xfffffffful));
#endif
        if((label && label_size<=volume.size()) || (fs && fs_size<=filesystem.size())){q.set_error(122);return 0;}
        if(label)q.memory.check(label,volume.size()+1,Memory::Write);if(fs)q.memory.check(fs,filesystem.size()+1,Memory::Write);for(auto out:{serial,length,flags})if(out)q.memory.check(out,4,Memory::Write);
        if(label)q.memory.copy_in(label,std::span(reinterpret_cast<const std::uint8_t*>(volume.c_str()),volume.size()+1));if(fs)q.memory.copy_in(fs,std::span(reinterpret_cast<const std::uint8_t*>(filesystem.c_str()),filesystem.size()+1));
        if(serial)q.memory.store(serial,number,32);if(length)q.memory.store(length,max_component,32);if(flags)q.memory.store(flags,attributes,32);return 1;
    });
    p.register_api("advapi32.dll","GetUserNameA",2,[](auto& q){
        // This is the login of the explicitly modelled guest process, not an
        // attempt to impersonate or disclose the host account.
        const std::string& name=q.options.user_name;const auto out=arg(q,0),count=arg(q,1),capacity=q.memory.load(count,32);q.memory.check(count,4,Memory::Write);
        if(capacity<=name.size()){q.memory.store(count,U32(name.size()+1),32);q.set_error(122);return 0u;}
        q.memory.check(out,name.size()+1,Memory::Write);q.memory.copy_in(out,std::span(reinterpret_cast<const std::uint8_t*>(name.c_str()),name.size()+1));q.memory.store(count,U32(name.size()+1),32);return 1u;
    });
    for(bool unicode:{false,true})kernel(p,unicode?"CompareStringW":"CompareStringA",6,[unicode](auto& q){
        const auto locale=arg(q,0),flags=arg(q,1),one=arg(q,2),two=arg(q,4);const auto n1=std::bit_cast<std::int32_t>(arg(q,3)),n2=std::bit_cast<std::int32_t>(arg(q,5));
        if(n1>(1<<20) || n2>(1<<20)){q.set_error(87);return 0u;}
#ifdef _WIN32
        int result{};
        if(unicode){auto a=n1<0?q.read_wstring(one):n1?wide(q,one,n1):std::u16string();auto b=n2<0?q.read_wstring(two):n2?wide(q,two,n2):std::u16string();result=::CompareStringW(locale,flags,reinterpret_cast<const wchar_t*>(a.c_str()),n1,reinterpret_cast<const wchar_t*>(b.c_str()),n2);}
        else {auto fetch=[&](U32 pointer,int count){if(count<0)return q.read_string(pointer);if(!count)return std::string();const auto bytes=narrow(q,pointer,count);return std::string(bytes.begin(),bytes.end());};auto a=fetch(one,n1),b=fetch(two,n2);result=::CompareStringA(locale,flags,a.c_str(),n1,b.c_str(),n2);}
        if(!result)q.set_error(::GetLastError());return U32(result);
#else
        (void)unicode;(void)locale;(void)flags;(void)one;(void)two;(void)n1;(void)n2;
        throw GuestFault(FaultKind::unsupported,q.cpu.eip,"portable Windows collation is not implemented; use the Windows host backend");return 0u;
#endif
    });
    kernel(p,"PeekNamedPipe",6,[](auto& q){auto it=q.state().files.find(arg(q,0));if(it==q.state().files.end()){q.set_error(6);return 0;}
        auto buffer=arg(q,1),size=arg(q,2),read=arg(q,3),available=arg(q,4),left=arg(q,5);if(size>64u*1024*1024){q.set_error(87);return 0;}
        for(auto out:{read,available,left})if(out)q.memory.check(out,4,Memory::Write);if(buffer && size)q.memory.check(buffer,size,Memory::Write);
        U32 nread{},nbytes{},nleft{};std::vector<std::uint8_t> bytes(buffer?size:0);
#ifdef _WIN32
        DWORD nr{},nb{},nl{};if(!::PeekNamedPipe(file_handle(it->second),buffer?bytes.data():nullptr,size,&nr,&nb,&nl)){q.set_error(::GetLastError());return 0;}nread=nr;nbytes=nb;nleft=nl;
#else
        if(it->second.type!=3){q.set_error(1);return 0;}
        if(buffer && size)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"portable pipe data peeking is not implemented");int count{};if(::ioctl(fileno(it->second.stream),FIONREAD,&count)){q.set_error(file_error(errno));return 0;}nbytes=U32(std::max(count,0));
#endif
        if(buffer && nread)q.memory.copy_in(buffer,std::span(bytes.data(),nread));if(read)q.memory.store(read,nread,32);if(available)q.memory.store(available,nbytes,32);if(left)q.memory.store(left,nleft,32);return 1;
    });
    kernel(p,"FormatMessageA",7,[](auto& q){const auto flags=arg(q,0),source=arg(q,1),message=arg(q,2),language=arg(q,3),out=arg(q,4),capacity=arg(q,5),arguments=arg(q,6);
        if(arguments || (flags&0x800u) || (flags&0x2000u))throw GuestFault(FaultKind::unsupported,q.cpu.eip,"FormatMessage argument arrays/module resources require explicit marshalling");
        if(capacity>64u*1024*1024){q.set_error(87);return 0u;}
        std::string text,input;if(flags&0x400u)input=q.read_string(source);
        if(!(flags&0x200u) && ((flags&0x1000u) || input.find('%')!=std::string::npos))throw GuestFault(FaultKind::unsupported,q.cpu.eip,"FormatMessage insert expansion requires explicitly marshalled arguments");
#ifdef _WIN32
        char* native{};const auto result=::FormatMessageA(flags|0x100u,(flags&0x400u)?input.c_str():nullptr,message,language,reinterpret_cast<char*>(&native),0,nullptr);
        if(!result){q.set_error(::GetLastError());return 0u;}text.assign(native,result);::LocalFree(native);
#else
        (void)message;(void)language;
        if((flags&~0x700u) || !(flags&0x400u) || input.find('%')!=std::string::npos)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"portable FormatMessage supports plain FROM_STRING only; native Windows formats system messages");text=input;
#endif
        if(flags&0x100u){q.memory.check(out,4,Memory::Write);auto at=q.allocate_bytes(std::max<std::size_t>(capacity,text.size()+1));q.state().global_blocks.emplace(at,U32(std::max<std::size_t>(capacity,text.size()+1)));q.memory.copy_in(at,std::span(reinterpret_cast<const std::uint8_t*>(text.c_str()),text.size()+1));q.memory.store(out,at,32);}
        else {if(capacity<=text.size()){q.set_error(122);return 0u;}q.memory.copy_in(out,std::span(reinterpret_cast<const std::uint8_t*>(text.c_str()),text.size()+1));}
        return U32(text.size());
    });
    kernel(p,"LocalFree",1,[](auto& q){auto at=arg(q,0);if(!at)return 0u;if(!q.state().global_blocks.erase(at)){q.set_error(6);return at;}q.memory.release(at);return 0u;});
    // A debugger-visible diagnostic is real output, not a substitute for UI.
    kernel(p,"OutputDebugStringA",1,[](auto& q){auto text=q.read_string(arg(q,0));std::fwrite(text.data(),1,text.size(),stderr);return 0;});
    for(const auto* name:{"GetLastError","SetLastError","GetCurrentThreadId","GetCurrentProcessId","TlsGetValue","TlsSetValue"})
        p.certify_leaf_api("kernel32.dll",name);
}
}
