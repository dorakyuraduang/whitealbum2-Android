// Own synthetic archives/RAM only; exercises the actual shared image ABI.
#include "process.hpp"
#include "../port/native_resources.hpp"
#include "../platform/compat/resource_mount.hpp"
#include "../engine/decoded_resource_cache.hpp"
#include <winrecomp/win32_state.hpp>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>

#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check at ")+std::to_string(__LINE__)+": "+#x);}while(false)
namespace r=wa2::resource;
using wr::U32;
using Bytes=std::vector<std::uint8_t>;
using Ring=std::array<std::uint8_t,4096>;
std::uint64_t number(const std::string& json,const std::string& key){const auto at=json.find('"'+key+"\":");CHECK(at!=std::string::npos);return std::stoull(json.substr(at+key.size()+3));}
unsigned reference(std::span<const std::uint8_t> input,std::span<std::uint8_t> output,Ring& ring){
    unsigned flags{},position=0xfee;std::size_t at{},written{};
    while(written<output.size()){
        flags>>=1;if(!(flags&0x100)){CHECK(at<input.size());flags=input[at++]|0xff00;}
        if(flags&1){CHECK(at<input.size());const auto value=input[at++];output[written++]=value;ring[position]=value;position=(position+1)&4095;}
        else{CHECK(at+2<=input.size());const unsigned low=input[at++],high=input[at++],offset=low|((high&0xf0)<<4),length=(high&15)+3;CHECK(length<=output.size()-written);
            for(unsigned n=0;n<length;++n){const auto value=ring[(offset+n)&4095];output[written++]=value;ring[position]=value;position=(position+1)&4095;}}
    }CHECK(at==input.size());return flags>>1;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned policies{},exact{},reuses{},invalidations{},tail_changes{};
    Ring empty{};const Bytes small(1024,7);
    // Cost admission is deterministic here; production measures only decode
    // work, excluding provider reads, allocation and capture-copy time.
    {r::DecodedResourceCache cache;const r::DecodedKey key{1,1};CHECK(!cache.find(key,std::span(empty).first<18>()));
        CHECK(!cache.capture_ready(key,small.size(),cache.ColdDecodeThresholdNs-1));
        cache.capture(key,small,true,3,0,std::span(empty).first<18>(),cache.ColdDecodeThresholdNs-1);CHECK(!cache.size());++policies;
        cache.capture(key,small,false,3,0,std::span(empty).first<18>(),cache.ColdDecodeThresholdNs);CHECK(!cache.size());++policies;
        cache.capture(key,small,true,3,1,std::span(empty).first<18>(),cache.ColdDecodeThresholdNs);CHECK(cache.size()==1 && cache.stats.cold_captures==1);
        auto hit=cache.find(key,std::span(empty).first<18>());CHECK(hit && hit->bytes==small);auto other=empty;other[0]=1;CHECK(!cache.find(key,std::span(other).first<18>()));++policies;
        CHECK(!cache.capture_ready(key,0,cache.ColdDecodeThresholdNs) && !cache.capture_ready(key,cache.MaximumEntryBytes+1,cache.ColdDecodeThresholdNs));++policies;
        const Bytes large(5u*1024*1024,33);for(unsigned n=0;n<3;++n){const r::DecodedKey item{2,n};cache.find(item,std::span(empty).first<18>());cache.capture(item,large,true,0,0,std::span(empty).first<18>(),cache.ColdDecodeThresholdNs);CHECK(cache.stats.bytes<=cache.MaximumBytes && cache.size()<=cache.MaximumEntries);}
        CHECK(cache.stats.evictions && hit->bytes==small && cache.stats.bytes==large.size());++policies;
    }
    {r::DecodedResourceCache cache;const r::DecodedKey key{3,1};const auto tail=std::span(empty).first<18>();
        CHECK(!cache.find(key,tail));cache.capture(key,small,true,0,0,tail);CHECK(!cache.size());
        CHECK(!cache.find(key,tail));cache.capture(key,small,true,0,0,tail);CHECK(cache.find(key,tail) && !cache.stats.cold_captures);++policies;
        CHECK(!cache.find({4,1},tail));++policies;
        for(unsigned n=0;n<40;++n){const r::DecodedKey item{5,n};cache.find(item,tail);cache.capture(item,small,true,0,0,tail,cache.ColdDecodeThresholdNs);}
        CHECK(cache.size()==cache.MaximumEntries && cache.stats.evictions && cache.stats.bytes<=cache.MaximumBytes);++policies;
    }
    struct Member {const char* name;Bytes packed;U32 expanded;};
    Member large{"large.dat",{},5u*1024*1024};
    std::mt19937 random(2819);unsigned written{};
    while(written<large.expanded){const auto flag=large.packed.size();large.packed.push_back(0);
        for(unsigned bit=0;bit<8 && written<large.expanded;++bit){const auto left=large.expanded-written;
            if(written<4096 || left<3 || (bit&1)){large.packed[flag]|=std::uint8_t(1u<<bit);large.packed.push_back(std::uint8_t(random()));++written;}
            else{const unsigned distance=1+random()%4096,offset=(0xfee + written-distance)&4095,length=std::min(18u,left);
                large.packed.push_back(std::uint8_t(offset));large.packed.push_back(std::uint8_t(((offset>>4)&0xf0)|(length-3)));written+=length;}}
    }
    std::array<Member,2> members{std::move(large),Member{"tail.dat",{0,0xee,0xf0,0xf1,0xf0},6}};
    Bytes archive(16+44*members.size());const auto put=[&](std::size_t at,U32 value){for(unsigned n=0;n<4;++n)archive[at+n]=std::uint8_t(value>>(n*8));};
    std::memcpy(archive.data(),"KCAP",4);put(12,U32(members.size()));
    for(unsigned n=0;n<members.size();++n){const auto& item=members[n];const auto at=16+n*44,offset=U32(archive.size()),stored=U32(item.packed.size()+8);
        put(at,1);std::memcpy(archive.data()+at+4,item.name,std::strlen(item.name));put(at+36,offset);put(at+40,stored);archive.resize(offset+stored);put(offset,stored);put(offset+4,item.expanded);std::copy(item.packed.begin(),item.packed.end(),archive.begin()+offset+8);}
    struct Versioned:wr::ReadOnlyFiles {
        std::shared_ptr<wr::ReadOnlyFiles> mount;std::uint64_t version{};
        std::optional<wr::ReadOnlyFileInfo> info(std::string_view name)const override{auto info=mount->info(name);if(info)info->modified_100ns=version;return info;}
        std::vector<wr::ReadOnlyFileInfo> list(std::string_view name)const override{return mount->list(name);}
        std::FILE* open(std::string_view name)override{return mount->open(name);}
    };
    for(bool linear:{false,true}){
        const auto root=std::filesystem::path(argv[1]).parent_path()/("resource-reuse-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(std::filesystem::create_directory(root));
        const auto path=root/"images.pak";{std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(archive.data()),std::streamsize(archive.size()));CHECK(file.good());}
        wr::ProcessOptions options;options.data_root=root;options.prefer_linear_memory=linear;wr::Process p(wr::FunctionExecutor{[](wr::Cpu&,wr::Memory&){throw std::runtime_error("unexpected static fallback");return false;}},options);wr::install_wa2_native_resources(p);
        auto mount=std::make_shared<Versioned>();mount->mount=wa2::platform::compat::local_resource_mount(root);wa2::platform::compat::install_resource_mount(p,mount);
        unsigned traces{};U32 message{};p.replace_api("kernel32.dll","OutputDebugStringA",1,[&](wr::Process& q){++traces;message=q.argument(0);return 0u;});p.load(wa2::aot::initial_image());
        const auto pak=p.put_string("images.pak"),slot=p.allocate_bytes(4096),heap=p.callback(p.resolve("kernel32.dll","GetProcessHeap"),{});std::array<U32,2> names;
        for(unsigned n=0;n<2;++n)names[n]=p.put_string(members[n].name);
        Ring incoming;for(unsigned n=0;n<4096;++n)incoming[n]=std::uint8_t(n*7+13);
        const auto report=[&]{return p.state().reports.at("native_resources")();};
        const auto load=[&](unsigned n){auto ring=incoming;std::fill_n(ring.begin(),0xfee,32);Bytes expected(members[n].expanded);const auto flags=reference(members[n].packed,expected,ring);
            p.memory.copy_in(0xa7cdd0,incoming);const auto trace_before=traces;CHECK(p.callback(0x437310,std::array<U32,3>{pak,names[n],slot},false)==expected.size());
            const auto pointer=p.memory.load(slot,32);CHECK(pointer);Bytes actual(expected.size());Ring final;p.memory.copy_out(pointer,actual);p.memory.copy_out(0xa7cdd0,final);
            CHECK(actual==expected && final==ring && traces==trace_before+1 && message==(flags&0x100?0x4a40ecu:0x4a40f0u));
            CHECK(p.callback(p.resolve("kernel32.dll","HeapFree"),std::array<U32,3>{heap,0,pointer}));++exact;
        };
        CHECK(p.state().resource_io_activity && !p.state().resource_io_activity());load(0);const auto first=report();
        const auto captured=number(first,"image_decoded_cache_cold_captures");load(0);const auto second=report();
        // The measured cost decides cold admission. If a future host decodes
        // below the threshold, the old repeat-only policy remains valid.
        CHECK(number(second,"image_decoded_cache_hits")==captured);reuses+=unsigned(captured);
        CHECK(number(second,"image_payload_reads")==2-captured);
        load(0);CHECK(number(report(),"image_decoded_cache_hits")==captured+1);++reuses;
        CHECK(number(report(),"image_payload_reads")==number(second,"image_payload_reads"));
        CHECK(number(report(),"image_decoded_cache_bytes")==members[0].expanded);
        const auto hits=number(report(),"image_decoded_cache_hits");++mount->version;load(0);CHECK(number(report(),"image_decoded_cache_hits")==hits);++invalidations;
        load(1);load(1);load(1);incoming[0xfee]^=23;load(1);incoming[0xff1]^=79;load(1);tail_changes+=2;
        const auto before=report();const auto saved=p.cpu;p.memory.protect(slot,4096,wr::Memory::Read);bool rejected{};
        try{p.callback(0x437310,std::array<U32,3>{pak,names[0],slot},false);}catch(const wr::GuestFault&){rejected=true;}
        CHECK(rejected);p.cpu=saved;p.memory.protect(slot,4096,wr::Memory::Read|wr::Memory::Write);++policies;
        CHECK(number(report(),"image_decoded_cache_bytes")<=r::DecodedResourceCache::MaximumBytes && p.state().resource_io_activity()>0 && !p.executed_instructions);
    }
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.resource-reuse.v1\",\"policy_and_boundary_cases\":"<<policies<<",\"exact_output_dictionary_trace_cases\":"<<exact<<",\"large_image_reuses\":"<<reuses<<",\"archive_invalidations\":"<<invalidations<<",\"caller_tail_changes\":"<<tail_changes
        <<",\"maximum_cache_bytes\":"<<r::DecodedResourceCache::MaximumBytes<<",\"cold_decode_threshold_ns\":"<<r::DecodedResourceCache::ColdDecodeThresholdNs<<",\"total_budget_unchanged\":true,\"original_resources_opened\":false,\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out.good());
    std::cout<<"PASS resource reuse: "<<exact<<" exact ABI loads, "<<reuses<<" large-image reuses, unchanged 8 MiB budget.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
