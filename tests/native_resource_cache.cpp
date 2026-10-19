// Native image loader against generated KCAP fixtures and owned RAM only.
// Includes dictionary-dependent streams, bounded eviction and paired latency.
#include "process.hpp"
#include "../port/native_resources.hpp"
#include "../platform/compat/resource_mount.hpp"
#include "../engine/decoded_resource_cache.hpp"
#include <winrecomp/win32_state.hpp>
#include <winrecomp/heap_cache.hpp>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace r=wa2::resource;
using wr::U32;
unsigned reference(std::span<const std::uint8_t> input,std::span<std::uint8_t> output,std::array<std::uint8_t,4096>& ring){
    unsigned flags{},position=0xfee;std::size_t at{},written{};
    while(written<output.size()){flags>>=1;if(!(flags&0x100)){CHECK(at<input.size());flags=input[at++]|0xff00;}
        if(flags&1){CHECK(at<input.size());const auto value=input[at++];output[written++]=value;ring[position]=value;position=(position+1)&4095;}
        else{CHECK(at+2<=input.size());const unsigned low=input[at++],high=input[at++],offset=low|((high&0xf0)<<4),length=(high&15)+3;CHECK(length<=output.size()-written);
            for(unsigned n=0;n<length;++n){const auto value=ring[(offset+n)&4095];output[written++]=value;ring[position]=value;position=(position+1)&4095;}}}
    CHECK(at==input.size());return flags>>1;
}
std::uint64_t number(const std::string& text,const std::string& key){const auto at=text.find('"'+key+"\":");CHECK(at!=std::string::npos);return std::stoull(text.substr(at+key.size()+3));}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},boundaries{},dependent_cases{},unchanged_rejections{};
    // Admission, input dependence, identity invalidation and memory bounds.
    {r::DecodedResourceCache cache;std::array<std::uint8_t,18> tail{};const std::vector<std::uint8_t> data(256,73);const r::DecodedKey key{1,2};
        CHECK(!cache.find(key,tail));CHECK(!cache.capture_ready(key,data.size()));cache.capture(key,data,true,17,1,tail);CHECK(!cache.size());++boundaries;
        CHECK(!cache.find(key,tail));cache.capture(key,data,true,17,1,tail);CHECK(cache.find(key,tail)->bytes==data);++boundaries;
        tail[17]=99;CHECK(cache.find(key,tail));tail[0]=3;CHECK(!cache.find(key,tail));++boundaries;
        CHECK(!cache.find({2,2},tail));CHECK(!cache.find({1,3},tail));++boundaries;
        const std::vector<std::uint8_t> large(3u*1024*1024,31);
        for(unsigned n=0;n<4;++n){const r::DecodedKey item{8,n};cache.find(item,tail);cache.find(item,tail);cache.capture(item,large,false,0,0,tail);}
        CHECK(cache.stats.bytes<=cache.MaximumBytes && cache.stats.evictions && cache.size()<=cache.MaximumEntries);++boundaries;
        CHECK(!cache.capture_ready({8,0},cache.MaximumEntryBytes+1));++boundaries;
        for(unsigned n=0;n<100;++n){const r::DecodedKey item{9,n};cache.find(item,tail);cache.find(item,tail);cache.capture(item,data,false,0,0,tail);}
        CHECK(cache.size()<=cache.MaximumEntries && cache.stats.bytes<=cache.MaximumBytes);++boundaries;
    }
    struct Member {std::string name;std::vector<std::uint8_t> stored;U32 expanded;bool packed;};
    std::vector<Member> members;
    std::vector<std::uint8_t> raw(65536);for(unsigned n=0;n<raw.size();++n)raw[n]=std::uint8_t(n*37+n/13);members.push_back({"raw.dat",raw,U32(raw.size()),false});
    std::vector<std::uint8_t> packed;constexpr unsigned Size=2u*1024*1024;unsigned written{};
    while(written<Size){const auto flag=packed.size();packed.push_back(0);
        for(unsigned n=0;n<8 && written<Size;++n){const auto remaining=Size-written;
            if(!written || remaining<3){packed[flag]|=std::uint8_t(1u<<n);packed.push_back(0x6d);++written;}
            else{const unsigned offset=(0xfee + written-1)&4095,length=std::min(18u,remaining);packed.push_back(std::uint8_t(offset));packed.push_back(std::uint8_t(((offset>>4)&0xf0)|(length-3)));written+=length;}}
    }
    members.push_back({"packed.dat",packed,Size,true});members.push_back({"tail.dat",{0,0xee,0xf0,0xf1,0xf0},6,true});
    const auto header=16+44*members.size();std::vector<std::uint8_t> archive(header);
    const auto put=[&](std::size_t at,U32 value){for(unsigned n=0;n<4;++n)archive[at+n]=std::uint8_t(value>>(n*8));};
    std::memcpy(archive.data(),"KCAP",4);put(12,U32(members.size()));
    for(unsigned n=0;n<members.size();++n){const auto& item=members[n];const std::size_t at=16+n*44,offset=archive.size(),bytes=item.stored.size()+(item.packed?8:0);
        put(at,item.packed);std::memcpy(archive.data()+at+4,item.name.data(),item.name.size());put(at+36,U32(offset));put(at+40,U32(bytes));archive.resize(offset+bytes);
        if(item.packed){put(offset,U32(bytes));put(offset+4,item.expanded);}std::copy(item.stored.begin(),item.stored.end(),archive.begin()+offset+(item.packed?8:0));}
    std::array<std::array<std::array<std::uint64_t,2>,5>,2> timing{};
    std::uint64_t cache_hits{},cache_reads_saved{};
    decltype(timing) allocation_timing{},allocation_commits{};
    for(unsigned mode=0;mode<2;++mode){const auto root=std::filesystem::path(argv[1]).parent_path()/("cache-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(std::filesystem::create_directory(root));
        const auto path=root/"images.pak";{std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(archive.data()),std::streamsize(archive.size()));CHECK(file.good());}
        wr::ProcessOptions options;options.data_root=root;options.prefer_linear_memory=bool(mode);wr::Process p(wr::FunctionExecutor{[](wr::Cpu&,wr::Memory&){return false;}},options);wr::install_wa2_native_resources(p);
        auto mount=std::make_shared<wa2::platform::compat::ResourceMount>(std::vector<wr::ReadOnlyFileInfo>{{"images.pak",archive.size(),0}},[&](std::string_view){return std::fopen(path.string().c_str(),"rb");});
        struct Versioned final:wr::ReadOnlyFiles {std::shared_ptr<wr::ReadOnlyFiles> mount;std::uint64_t version{};
            std::optional<wr::ReadOnlyFileInfo> info(std::string_view name)const override{auto value=mount->info(name);if(value)value->modified_100ns=version;return value;}
            std::vector<wr::ReadOnlyFileInfo> list(std::string_view name)const override{return mount->list(name);}
            std::FILE* open(std::string_view name)override{return mount->open(name);}
        };
        auto versioned=std::make_shared<Versioned>();versioned->mount=mount;wa2::platform::compat::install_resource_mount(p,versioned);
        unsigned traces{};U32 last_trace{};p.replace_api("kernel32.dll","OutputDebugStringA",1,[&](wr::Process& q){++traces;last_trace=q.argument(0);return 0u;});p.load(wa2::aot::initial_image());
        const auto pak=p.put_string("images.pak"),slot=p.allocate_bytes(4096),heap=p.callback(p.resolve("kernel32.dll","GetProcessHeap"),{});
        std::array<U32,3> names;for(unsigned n=0;n<3;++n)names[n]=p.put_string(members[n].name);
        std::array<std::uint8_t,4096> incoming;for(unsigned n=0;n<incoming.size();++n)incoming[n]=std::uint8_t(n*7+3);
        const auto load=[&](unsigned member,bool check){const auto& item=members[member];p.memory.copy_in(0xa7cdd0,incoming);const auto before=traces;
            const auto size=p.callback(0x437310,std::array<U32,3>{pak,names[member],slot},false);CHECK(size==item.expanded);const auto pointer=p.memory.load(slot,32);CHECK(pointer);
            std::uint64_t checksum{};for(unsigned at:{0u,size/2,size-1})checksum=checksum*257+p.memory.load(pointer+at,8);
            if(check){auto ring=incoming;std::vector<std::uint8_t> expected(size),actual(size);unsigned flags{};
                if(item.packed){std::fill_n(ring.begin(),0xfee,32);flags=reference(item.stored,expected,ring);}else expected=item.stored;
                p.memory.copy_out(pointer,actual);CHECK(actual==expected);std::array<std::uint8_t,4096> final_ring;p.memory.copy_out(0xa7cdd0,final_ring);CHECK(final_ring==ring);
                CHECK(traces-before==unsigned(item.packed));if(item.packed)CHECK(last_trace==(flags&0x100?0x4a40ec:0x4a40f0));++cases;
            }
            p.memory.store(pointer,p.memory.load(pointer,8)^255u,8); // caller owns this copy
            CHECK(p.callback(p.resolve("kernel32.dll","HeapFree"),std::array<U32,3>{heap,0,pointer})==1);return checksum;
        };
        for(unsigned member=0;member<3;++member){for(unsigned repeat=0;repeat<5;++repeat){if(member==2 && repeat>=2)incoming[0xfee + 17]++;load(member,true);}}
        for(unsigned repeat=0;repeat<4;++repeat){if(repeat==0)++incoming[0xfee];else ++incoming[0xfee + 17];load(2,true);++dependent_cases;}
        const auto before_report=p.state().reports.at("native_resources")();CHECK(number(before_report,"image_decoded_cache_hits")>=11);
        cache_hits+=number(before_report,"image_decoded_cache_hits");cache_reads_saved+=number(before_report,"image_decoded_cache_hits");
        for(unsigned pair=0;pair<5;++pair){std::array<std::uint64_t,2> checksums{};
            for(unsigned order=0;order<2;++order){const auto side=(pair+order)&1;std::optional<r::DecodedCacheReferenceScope> reference_scope;if(!side)reference_scope.emplace();
                const auto begin=std::chrono::steady_clock::now();for(unsigned n=0;n<4;++n)checksums[side]=checksums[side]*0x100000001b3ull+load(1,false);
                timing[mode][pair][side]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();}
            CHECK(checksums[0]==checksums[1]);}
        for(unsigned pair=0;pair<5;++pair){std::array<std::uint64_t,2> checksums{};
            for(unsigned order=0;order<2;++order){const auto side=(pair+order)&1;wr::set_large_heap_cache_enabled(p,bool(side));load(1,false);
                const auto commits=p.memory.native_operations().commits;const auto begin=std::chrono::steady_clock::now();
                for(unsigned n=0;n<4;++n)checksums[side]=checksums[side]*0x100000001b3ull+load(1,false);
                allocation_timing[mode][pair][side]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
                allocation_commits[mode][pair][side]=p.memory.native_operations().commits-commits;
                if(mode)CHECK(allocation_commits[mode][pair][side]==(side?0u:4u));
            }CHECK(checksums[0]==checksums[1]);}
        wr::set_large_heap_cache_enabled(p,true);
        const auto old_hits=number(p.state().reports.at("native_resources")(),"image_decoded_cache_hits");
        ++versioned->version;load(1,true);load(1,true);CHECK(number(p.state().reports.at("native_resources")(),"image_decoded_cache_hits")==old_hits);
        load(1,true);CHECK(number(p.state().reports.at("native_resources")(),"image_decoded_cache_hits")==old_hits+1);
        CHECK(number(p.state().reports.at("native_resources")(),"image_index_opens")==2);
        // Cache presence must not weaken the native output-slot guard.
        const auto saved=p.cpu;const auto before=incoming;p.memory.copy_in(0xa7cdd0,incoming);p.memory.protect(slot,4096,wr::Memory::Read);bool rejected{};
        try{p.callback(0x437310,std::array<U32,3>{pak,names[1],slot},false);}catch(const wr::GuestFault&){rejected=true;}CHECK(rejected);p.cpu=saved;
        p.memory.protect(slot,4096,wr::Memory::Read|wr::Memory::Write);std::array<std::uint8_t,4096> after;p.memory.copy_out(0xa7cdd0,after);CHECK(after==before);++unchanged_rejections;
        CHECK(!p.executed_instructions);
    }
    std::ofstream out(argv[1]);out<<"{\"exact_output_dictionary_and_trace_cases\":"<<cases<<",\"cache_boundary_cases\":"<<boundaries<<",\"caller_dictionary_change_cases\":"<<dependent_cases
        <<",\"unchanged_rejections\":"<<unchanged_rejections<<",\"native_cache_hits_checked\":"<<cache_hits<<",\"repeat_payload_reads_avoided\":"<<cache_reads_saved
        <<",\"maximum_cache_bytes\":"<<r::DecodedResourceCache::MaximumBytes<<",\"paired_loads_per_side\":4,\"timing\":[";
    for(unsigned mode=0;mode<2;++mode){if(mode)out<<',';out<<"{\"linear_memory\":"<<(mode?"true":"false")<<",\"paired_ns\":[";
        for(unsigned n=0;n<5;++n){if(n)out<<',';out<<'['<<timing[mode][n][0]<<','<<timing[mode][n][1]<<']';}out<<"]}";}
    out<<"],\"large_allocation_timing\":[";
    for(unsigned mode=0;mode<2;++mode){if(mode)out<<',';out<<"{\"linear_memory\":"<<(mode?"true":"false")<<",\"paired_ns\":[";
        for(unsigned n=0;n<5;++n){if(n)out<<',';out<<'['<<allocation_timing[mode][n][0]<<','<<allocation_timing[mode][n][1]<<']';}
        out<<"],\"paired_native_commit_calls\":[";for(unsigned n=0;n<5;++n){if(n)out<<',';out<<'['<<allocation_commits[mode][n][0]<<','<<allocation_commits[mode][n][1]<<']';}out<<"]}";}
    out<<"],\"maximum_idle_large_allocation_bytes\":4194304,\"original_resources_opened\":false,\"game_launched\":false,\"screen_access\":false,\"device_fps_measured\":false,\"passed\":true}\n";
    std::cout<<"PASS bounded image cache: "<<cases<<" exact loads, "<<dependent_cases<<" caller-tail changes, "<<cache_hits<<" checked reuses.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
