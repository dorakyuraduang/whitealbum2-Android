// This file is the sole resource ABI adapter. Archive I/O, lookup and decoding
// live in ordinary C++ modules and never execute translated instructions.
#include "native_resources.hpp"
#include "guest_api.hpp"
#include "resource_archive.hpp"
#include "decoded_resource_cache.hpp"
#include "file_io.hpp"
#include "native_backend.hpp"
#include "clock.hpp"
#include "../platform/compat/resource_mount.hpp"
#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>

namespace wr {
namespace {
struct ResourceState {
    struct Cached {std::FILE* stream{};std::filesystem::path path;wa2::resource::Archive archive;};
    struct PathCached {std::uintmax_t size{};std::uint64_t modified{},used{},identity{};wa2::resource::Archive archive;};
    struct Timing {std::uint64_t calls{},total{},maximum{};};
    struct Timer {
        Timing& value;std::uint64_t started{wa2::platform::Clock::counter()};
        explicit Timer(Timing& v):value(v){}
        ~Timer(){const auto ns=wa2::platform::Clock::counter()-started;++value.calls;value.total+=ns;value.maximum=std::max(value.maximum,ns);}
    };
    std::map<U32,Cached> archives;
    std::map<std::filesystem::path,PathCached> path_archives;
    wa2::resource::DecodedResourceCache decoded_images;
    std::map<std::string,Timing> timing;
    bool tracing{[]{const auto* value=std::getenv("WA2_FRAME_TRACE");return value && std::string_view(value)=="1";}()};
    struct LoadTrace {std::uint64_t start{},elapsed{};U32 bytes{};std::string member;};
    std::deque<LoadTrace> load_trace;
    std::uint64_t lookups{},loads{},decodes{},image_decodes{},single_pass_images{},image_loads{},image_checks{},missing_archives{},bytes{},declined{},errors{},index_opens{},index_hits{},cache_clock{};
    std::uint64_t direct_resource_decodes{},resource_staging_bytes_avoided{};
    U32 heap_alloc{},heap_free{},get_heap{},debug_string{};
    bool supported(Process& p,U32 entry) {
        if(p.input_sha256=="246a263b965147a274fa59cdd7a72f6be10ba063d8cf4e61e74baefb56f29327" && wa2_native_code_verified(p.memory,entry))return true;
        ++declined;return false;
    }
    const PathCached& get(Process& p,const std::filesystem::path& path) {
        Timer time(timing["image_index"]);
        // Archives are immutable during normal play. Still invalidate their
        // directory when size or modification time changes between loads.
        const auto mounted=wa2::platform::compat::mounted_resource_info(p,path);
        const auto size=mounted?mounted->size:std::filesystem::file_size(path);
        const auto modified=mounted?mounted->modified_100ns:std::uint64_t(std::filesystem::last_write_time(path).time_since_epoch().count());
        auto found=path_archives.find(path);
        if(found!=path_archives.end() && (found->second.size!=size || found->second.modified!=modified)) {
            path_archives.erase(found);found=path_archives.end();
        }
        if(found!=path_archives.end()){++index_hits;found->second.used=++cache_clock;return found->second;}
        auto archive=wa2::resource::Archive::open(wa2::platform::compat::open_process_resource(p,path));++index_opens;
        if(path_archives.size()>=8){auto oldest=std::min_element(path_archives.begin(),path_archives.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});path_archives.erase(oldest);}
        const auto identity=++cache_clock;
        return path_archives.emplace(path,PathCached{size,modified,identity,identity,std::move(archive)}).first->second;
    }
    const wa2::resource::Archive* get(Process& p,U32 manager,U32 index) {
        for(auto it=archives.begin();it!=archives.end();) {
            if(!p.state().files.contains(it->first))it=archives.erase(it);else ++it;
        }
        const auto begin=p.memory.load(manager+12,32),end=p.memory.load(manager+16,32);
        if(end<begin || (end-begin)%0x148 || index>=(end-begin)/0x148)return nullptr;
        const auto record=begin+index*0x148,handle=p.memory.load(record,32),count=p.memory.load(record+4,32);
        const auto file=p.state().files.find(handle);
        if(file==p.state().files.end() || !file->second.read || file->second.write || file->second.path.empty())return nullptr;
        auto found=archives.find(handle);
        if(found!=archives.end() && (found->second.stream!=file->second.stream || found->second.path!=file->second.path)) {
            archives.erase(found);found=archives.end();
        }
        if(found==archives.end()) {
            auto archive=wa2::resource::Archive::open(wa2::platform::compat::open_process_resource(p,file->second.path));
            found=archives.emplace(handle,Cached{file->second.stream,file->second.path,std::move(archive)}).first;
        }
        return found->second.archive.members().size()==count ? &found->second.archive : nullptr;
    }
    bool matches(Process& p,U32 manager,U32 archive,std::size_t index,const wa2::resource::Member& member) {
        const auto record=p.memory.load(manager+12,32)+archive*0x148;
        const auto table=p.memory.load(record+0x110,32),entry=table+U32(index*40);
        std::array<std::uint8_t,31> name{};p.memory.copy_out(entry,name);
        const auto length=std::size_t(std::find(name.begin(),name.end(),0)-name.begin());
        return p.memory.load(entry+32,32)==member.stored_size && p.memory.load(entry+36,32)==member.offset &&
            bool(p.memory.load(entry+31,8))==member.compressed &&
            wa2::resource::compare_names(std::string_view(reinterpret_cast<const char*>(name.data()),length),member.name)==0;
    }
    bool direct_resource_memory(Process& p) {auto scope=p.memory.execution_scope();return scope.linear_base()!=nullptr;}
    // Packed input has already been validated before the legacy file seek and
    // allocator callbacks. Its owned snapshot survives callbacks and aliasing.
    U32 allocate(Process& p,std::span<const std::uint8_t> data,U32 output,std::optional<U32> expanded={}) {
        const auto size=expanded.value_or(U32(data.size()));
        p.memory.check(output,4,Memory::Write);
        const auto heap=p.callback(get_heap,{});
        const std::array<U32,3> args{heap,0,size+1};
        const auto result=p.callback(heap_alloc,args);
        if(!result){p.memory.store(output,0,32);return 0;}
        bool direct{};
        try {
            if(expanded){
                auto scope=p.memory.execution_scope();auto* base=scope.linear_base();
                if(base){
                    p.memory.check(result,size,Memory::Write);
                    wa2::resource::decode_lzss(data,{base+result,size});direct=true;
                }else{
                    // An allocator hook may change mappings after admission.
                    // Keep the ordinary checked copy for paged/executable data.
                    std::vector<std::uint8_t> staged(size);wa2::resource::decode_lzss(data,staged);p.memory.copy_in(result,staged);
                }
            }else p.memory.copy_in(result,data);
            p.memory.store(result+size,0,8);p.memory.store(output,result,32);
        }
        catch(...) {p.callback(heap_free,std::array<U32,3>{heap,0,result});throw;}
        if(direct){++direct_resource_decodes;resource_staging_bytes_avoided+=size;}
        return size;
    }
    void trace(Process& p,unsigned flags) {
        const std::array<U32,1> message{flags&0x100?0x4a40ecu:0x4a40f0u};p.callback(debug_string,message);
    }
};
}
void install_wa2_native_resources(Process& p,bool image_directory_functions) {
    auto state=std::make_shared<ResourceState>();
    state->heap_alloc=p.resolve("kernel32.dll","HeapAlloc");state->heap_free=p.resolve("kernel32.dll","HeapFree");
    state->get_heap=p.resolve("kernel32.dll","GetProcessHeap");
    state->debug_string=p.resolve("kernel32.dll","OutputDebugStringA");
    p.register_native_function(0x437000,4,[state](Process& q)->std::optional<U32> {
        if(!state->supported(q,0x437000))return std::nullopt;
        ResourceState::Timer total(state->timing["image_decoder"]);
        const auto output=q.argument(0),input=q.argument(1),compressed=q.argument(2),expanded=q.argument(3);
        constexpr U32 dictionary=0xa7cdd0;
        auto overlap=[](U32 a,U32 an,U32 b,U32 bn){return std::uint64_t(a)<std::uint64_t(b)+bn && std::uint64_t(b)<std::uint64_t(a)+an;};
        if(compressed>128u*1024*1024 || expanded>128u*1024*1024 ||
            overlap(input,compressed,output,expanded) || overlap(input,compressed,dictionary,4096) || overlap(output,expanded,dictionary,4096) ||
            q.cpu.r[ESP]<64 || overlap(output,expanded,q.cpu.r[ESP]-64,84)) {
            ++state->declined;return std::nullopt;
        }
        unsigned flags{};
        {
            auto scope=q.memory.execution_scope();const auto* base=scope.linear_base();
            std::vector<std::uint8_t> staged_source,staged_output;std::array<std::uint8_t,4096> staged_ring;
            try {
                q.memory.check(input,compressed,Memory::Read);q.memory.check(output,expanded,Memory::Write);
                q.memory.check(dictionary,4096,Memory::Read|Memory::Write);
                const auto* pages=scope.native_pages();
                for(std::uint64_t at=output;at<std::uint64_t(output)+expanded;at=(at|4095ull)+1)
                    if((*pages[at>>22])[(at>>12)&1023].permissions&Memory::Execute){++state->declined;return std::nullopt;}
                for(std::uint64_t at=dictionary;at<std::uint64_t(dictionary)+4096;at=(at|4095ull)+1)
                    if((*pages[at>>22])[(at>>12)&1023].permissions&Memory::Execute){++state->declined;return std::nullopt;}
                if(!base){staged_source.resize(compressed);q.memory.copy_out(input,staged_source);staged_output.resize(expanded);q.memory.copy_out(dictionary,staged_ring);}
                const auto source=base?std::span<const std::uint8_t>(base+input,compressed):std::span<const std::uint8_t>(staged_source);
                // Validate before changing either legacy destination. Malformed
                // input retains the original diagnostic/fault behavior.
                ResourceState::Timer time(state->timing["validate"]);wa2::resource::validate_lzss(source,expanded);
            } catch(const GuestFault&) {++state->declined;return std::nullopt;}
              catch(const wa2::resource::Error&) {++state->declined;return std::nullopt;}
            auto* writable=const_cast<std::uint8_t*>(base);
            auto ring=base?std::span<std::uint8_t,4096>(writable+dictionary,4096):std::span<std::uint8_t,4096>(staged_ring);std::fill_n(ring.begin(),0xfee,32);
            ResourceState::Timer time(state->timing["decode"]);
            flags=wa2::resource::decode_lzss_dictionary(base?std::span<const std::uint8_t>(base+input,compressed):std::span<const std::uint8_t>(staged_source),
                base?std::span<std::uint8_t>(writable+output,expanded):std::span<std::uint8_t>(staged_output),ring);
            if(!base){q.memory.copy_in(output,staged_output);q.memory.copy_in(dictionary,staged_ring);}
        }
        // The original's terminating trace contains a fixed full-width digit,
        // so preserve its OutputDebugString effect without a variadic wrapper.
        state->trace(q,flags);
        ++state->image_decodes;++state->decodes;state->bytes+=expanded;return expanded;
    },true);
    if(image_directory_functions)p.register_native_function(0x437220,2,[state](Process& q)->std::optional<U32> {
        if(!state->supported(q,0x437220))return std::nullopt;
        ResourceState::Timer time(state->timing["image_check"]);
        try {
            const auto path=guest_file_path(q,q.read_string(q.argument(0),32768));
            for(const auto& [id,file]:q.state().files)if(file.path==path && (file.write || !(file.share&1))){++state->declined;return std::nullopt;}
            const auto& archive=state->get(q,path).archive;if(!archive.is_kcap()){++state->declined;return std::nullopt;}
            const auto member=archive.find_first(q.read_string(q.argument(1),1024));++state->image_checks;
            return member==wa2::resource::Archive::missing ? 0 : archive.members()[member].source_index+1;
        }catch(const std::filesystem::filesystem_error& error){
            if(error.code()==std::errc::no_such_file_or_directory){++state->missing_archives;q.set_error(2);return 0;}
            ++state->declined;return std::nullopt;
        }
         catch(const wa2::resource::Error&){++state->declined;return std::nullopt;}
         catch(const std::bad_alloc&){++state->declined;return std::nullopt;}
    },true);
    if(image_directory_functions)p.register_native_function(0x437310,3,[state](Process& q)->std::optional<U32> {
        if(!state->supported(q,0x437310))return std::nullopt;
        ResourceState::Timer total(state->timing["image_load"]);
        constexpr U32 dictionary=0xa7cdd0;
        const auto output=q.argument(2);
        wa2::resource::StoredResource stored;
        wa2::resource::DecodedKey cache_key{};std::array<std::uint8_t,18> input_tail{};
        std::shared_ptr<const wa2::resource::DecodedMember> cached;
        std::filesystem::path image_path;std::size_t image_member{};
        try {
            const auto path=guest_file_path(q,q.read_string(q.argument(0),32768));image_path=path;
            for(const auto& [id,file]:q.state().files)if(file.path==path && (file.write || !(file.share&1))){++state->declined;return std::nullopt;}
            const auto& indexed=state->get(q,path);const auto& archive=indexed.archive;if(!archive.is_kcap()){++state->declined;return std::nullopt;}
            const auto member=archive.find_first(q.read_string(q.argument(1),1024));
            if(member==wa2::resource::Archive::missing)return 0;
            cache_key={indexed.identity,member};image_member=member;
            if(archive.members()[member].compressed)q.memory.copy_out(dictionary+0xfee,input_tail);
            {ResourceState::Timer time(state->timing["image_cache_lookup"]);cached=state->decoded_images.find(cache_key,input_tail);}
            if(cached){stored.expanded_size=U32(cached->bytes.size());stored.compressed=cached->compressed;stored.dictionary_fill=archive.members()[member].dictionary_fill;}
            else{ResourceState::Timer time(state->timing["image_read"]);stored=archive.read(member);}
        } catch(const std::filesystem::filesystem_error& error) {
            if(error.code()==std::errc::no_such_file_or_directory){++state->missing_archives;q.set_error(2);return 0;}
            ++state->declined;return std::nullopt;
        }
          catch(const wa2::resource::Error&) {++state->declined;return std::nullopt;}
          catch(const std::bad_alloc&) {++state->declined;return std::nullopt;}
          catch(const GuestFault&) {++state->declined;return std::nullopt;}
        if(q.cpu.r[ESP]<64 || (std::uint64_t(output)<std::uint64_t(q.cpu.r[ESP])+16 && std::uint64_t(q.cpu.r[ESP])-64<std::uint64_t(output)+4)){
            ++state->declined;return std::nullopt;
        }
        q.memory.check(output,4,Memory::Write);
        {
            auto scope=q.memory.execution_scope();const auto* pages=scope.native_pages();
            if(stored.compressed)q.memory.check(dictionary,4096,Memory::Read|Memory::Write);
            auto executable=[&](U32 address,U32 size){
                for(std::uint64_t at=address;at<std::uint64_t(address)+size;at=(at|4095ull)+1)
                    if((*pages[at>>22])[(at>>12)&1023].permissions&Memory::Execute)return true;
                return false;
            };
            if(executable(output,4) || (stored.compressed && (executable(dictionary,4096) || (output<dictionary+4096 && std::uint64_t(output)+4>dictionary)))){
                ++state->declined;return std::nullopt;
            }
        }
        const auto heap=q.callback(state->get_heap,{});U32 pointer{};
        {ResourceState::Timer time(state->timing["image_allocate"]);pointer=q.callback(state->heap_alloc,std::array<U32,3>{heap,0,stored.expanded_size});}
        if(!pointer){++state->errors;q.memory.store(output,0,32);return 0;}
        unsigned flags{},tail_reads{};
        try {
            if(stored.compressed){
                auto scope=q.memory.execution_scope();auto* base=const_cast<std::uint8_t*>(scope.linear_base());
                q.memory.check(pointer,stored.expanded_size,Memory::Write);
                // This allocation is private until decoding succeeds. Keep the
                // dictionary private too, so the checked decoder can validate
                // while producing bytes, without a second token-stream pass.
                std::array<std::uint8_t,4096> ring{};
                std::fill_n(ring.begin(),0xfee,stored.dictionary_fill);
                q.memory.copy_out(dictionary+0xfee,std::span(ring).subspan(0xfee,18));
                std::copy_n(ring.begin()+0xfee,18,input_tail.begin());
                if(cached && !cached->matches(input_tail)){
                    // A native heap hook may change the caller-owned tail.
                    // Re-read/decode for that input, retaining this allocation.
                    const auto expected=stored.expanded_size;
                    ResourceState::Timer time(state->timing["image_read"]);stored=state->get(q,image_path).archive.read(image_member);cached.reset();
                    if(stored.expanded_size!=expected)throw wa2::resource::Error("image archive changed during allocation");
                }
                std::vector<std::uint8_t> staged_output;if(!base)staged_output.resize(stored.expanded_size);
                const auto produced=base?std::span<std::uint8_t>(base+pointer,stored.expanded_size):std::span<std::uint8_t>(staged_output);
                if(cached){ResourceState::Timer time(state->timing["image_cache_copy"]);std::memcpy(produced.data(),cached->bytes.data(),produced.size());
                    wa2::resource::finish_lzss_dictionary(produced,ring);flags=cached->flags;
                }else{
                    {ResourceState::Timer time(state->timing["decode"]);flags=wa2::resource::decode_lzss_dictionary(stored.bytes,produced,ring,&tail_reads);}
                    try{state->decoded_images.capture(cache_key,produced,true,flags,tail_reads,input_tail);}catch(const std::bad_alloc&){}
                }
                if(!base)q.memory.copy_in(pointer,staged_output);q.memory.copy_in(dictionary,ring);
            }else {
                {ResourceState::Timer time(state->timing[cached?"image_cache_copy":"image_copy"]);q.memory.copy_in(pointer,cached?std::span<const std::uint8_t>(cached->bytes):std::span<const std::uint8_t>(stored.bytes));}
                if(!cached)try{state->decoded_images.capture(cache_key,stored.bytes,false,0,0,input_tail);}catch(const std::bad_alloc&){}
            }
            q.memory.store(output,pointer,32);
        }catch(const wa2::resource::Error&){
            q.callback(state->heap_free,std::array<U32,3>{heap,0,pointer});
            ++state->declined;return std::nullopt;
        }catch(...){q.callback(state->heap_free,std::array<U32,3>{heap,0,pointer});throw;}
        if(stored.compressed){state->trace(q,flags);if(!cached){++state->decodes;++state->image_decodes;++state->single_pass_images;}}
        ++state->loads;++state->image_loads;state->bytes+=stored.expanded_size;
        if(state->tracing){state->load_trace.push_back({total.started,wa2::platform::Clock::counter()-total.started,stored.expanded_size,q.read_string(q.argument(1),1024)});if(state->load_trace.size()>128)state->load_trace.pop_front();}
        return stored.expanded_size;
    },true);
    p.register_native_function(0x4588a0,3,[state](Process& q)->std::optional<U32> {
        if(!state->supported(q,0x4588a0))return std::nullopt;
        try {
            const auto manager=q.cpu.r[ECX],index=q.argument(0);const auto* archive=state->get(q,manager,index);
            if(!archive){++state->declined;return std::nullopt;}
            const auto found=archive->find(q.read_string(q.argument(1),1024));
            // The diagnostic branch can invoke a mutable game callback.
            if(found==wa2::resource::Archive::missing && q.argument(2)){++state->declined;return std::nullopt;}
            if(found!=wa2::resource::Archive::missing && !state->matches(q,manager,index,found,archive->members()[found])) {
                ++state->declined;return std::nullopt;
            }
            ++state->lookups;return found==wa2::resource::Archive::missing ? 0xffffffffu : U32(found);
        } catch(const wa2::resource::Error&) {++state->declined;return std::nullopt;}
          catch(const std::bad_alloc&) {++state->declined;return std::nullopt;}
    });
    p.register_native_function(0x458050,3,[state](Process& q)->std::optional<U32> {
        if(!state->supported(q,0x458050))return std::nullopt;
        const auto manager=q.cpu.r[ECX],index=q.argument(0),member=q.argument(1),output=q.argument(2);
        try {
            const auto* archive=state->get(q,manager,index);
            if(!archive || member>=archive->members().size() || !state->matches(q,manager,index,member,archive->members()[member])) {
                ++state->declined;return std::nullopt;
            }
            // This legacy loader uses a zero dictionary, even when a fixture
            // exposes KCAP data through its normalized record format.
            auto stored=archive->read(member,128u*1024*1024);
            const bool direct=stored.compressed && state->direct_resource_memory(q);
            if(direct)wa2::resource::validate_lzss(stored.bytes,stored.expanded_size);
            else if(stored.compressed){
                std::vector<std::uint8_t> decoded(stored.expanded_size);wa2::resource::decode_lzss(stored.bytes,decoded);
                stored.bytes=std::move(decoded);
            }
            const auto record=q.memory.load(manager+12,32)+index*0x148,handle=q.memory.load(record,32);
            const auto& entry=archive->members()[member];
            if(!wa2::platform::seek_stream(q.state().files.at(handle).stream,entry.offset+entry.stored_size)) {
                ++state->errors;q.memory.store(output,0,32);return 0;
            }
            const auto result=state->allocate(q,stored.bytes,output,direct?std::optional<U32>(stored.expanded_size):std::nullopt);
            if(result || !stored.expanded_size){++state->loads;state->bytes+=stored.expanded_size;if(entry.compressed)++state->decodes;}
            return result;
        } catch(const wa2::resource::Error&) {++state->errors;q.memory.store(output,0,32);return 0;}
          catch(const std::bad_alloc&) {++state->errors;q.memory.store(output,0,32);return 0;}
    });
    p.register_native_function(0x457010,3,[state](Process& q)->std::optional<U32> {
        if(!state->supported(q,0x457010))return std::nullopt;
        const auto compressed=q.argument(0),input=q.argument(1),output=q.argument(2);
        q.memory.check(output,4,Memory::Write);
        try {
            const auto expanded=q.memory.load(input,32);
            if(compressed>128u*1024*1024 || expanded>128u*1024*1024)throw wa2::resource::Error("resource allocation limit");
            std::vector<std::uint8_t> source(compressed);q.memory.copy_out(input+4,source);
            const bool direct=state->direct_resource_memory(q);U32 size{};
            if(direct){
                wa2::resource::validate_lzss(source,expanded);size=state->allocate(q,source,output,expanded);
            }else{
                std::vector<std::uint8_t> data(expanded);wa2::resource::decode_lzss(source,data);size=state->allocate(q,data,output);
            }
            if(size || !expanded){++state->decodes;state->bytes+=expanded;}return size;
        } catch(const wa2::resource::Error&) {++state->errors;q.memory.store(output,0,32);return 0;}
          catch(const std::bad_alloc&) {++state->errors;q.memory.store(output,0,32);return 0;}
    });
    p.state().reports["native_resources"]=[state] {
        std::ostringstream detail;detail<<"[";bool first=true;
        for(const auto& [name,time]:state->timing){if(!first)detail<<',';first=false;detail<<"{\"stage\":\""<<name<<"\",\"calls\":"<<time.calls<<",\"total_ns\":"<<time.total<<",\"max_ns\":"<<time.maximum<<'}';}detail<<"]";
        return "{\"backend\":\"portable-cpp-archive\",\"lookups\":"+std::to_string(state->lookups)+
            ",\"loads\":"+std::to_string(state->loads)+",\"decodes\":"+std::to_string(state->decodes)+
            ",\"image_decodes\":"+std::to_string(state->image_decodes)+
            ",\"single_pass_image_decodes\":"+std::to_string(state->single_pass_images)+
            ",\"direct_resource_decodes\":"+std::to_string(state->direct_resource_decodes)+
            ",\"resource_staging_bytes_avoided\":"+std::to_string(state->resource_staging_bytes_avoided)+
            ",\"image_loads\":"+std::to_string(state->image_loads)+",\"image_index_opens\":"+std::to_string(state->index_opens)+
            ",\"image_checks\":"+std::to_string(state->image_checks)+
            ",\"image_missing_archives\":"+std::to_string(state->missing_archives)+
            ",\"image_index_cache_hits\":"+std::to_string(state->index_hits)+",\"image_cached_archives\":"+std::to_string(state->path_archives.size())+
            ",\"image_decoded_cache_hits\":"+std::to_string(state->decoded_images.stats.hits)+",\"image_decoded_cache_misses\":"+std::to_string(state->decoded_images.stats.misses)+
            ",\"image_decoded_cache_evictions\":"+std::to_string(state->decoded_images.stats.evictions)+",\"image_decoded_cache_bytes\":"+std::to_string(state->decoded_images.stats.bytes)+
            ",\"image_decoded_cache_maximum_bytes\":"+std::to_string(wa2::resource::DecodedResourceCache::MaximumBytes)+
            ",\"decoded_bytes\":"+std::to_string(state->bytes)+",\"declined\":"+std::to_string(state->declined)+
            ",\"errors\":"+std::to_string(state->errors)+",\"cached_archives\":"+std::to_string(state->archives.size())+",\"timing\":"+detail.str()+"}";
    };
    p.state().reports["resource_load_trace"]=[state]{
        std::ostringstream out;out<<"{\"enabled\":"<<(state->tracing?"true":"false")<<",\"columns\":[\"steady_start_ns\",\"elapsed_ns\",\"bytes\",\"member\"],\"recent\":[";bool first=true;
        for(const auto& v:state->load_trace){if(!first)out<<',';first=false;out<<'['<<v.start<<','<<v.elapsed<<','<<v.bytes<<",\"";constexpr char hex[]="0123456789abcdef";for(const unsigned char c:v.member){if(c>=32 && c<127 && c!='"' && c!='\\')out<<char(c);else out<<"\\u00"<<hex[c>>4]<<hex[c&15];}out<<"\"]";}out<<"]}";return out.str();
    };
}
}
