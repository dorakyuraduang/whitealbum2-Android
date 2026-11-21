#include "../recompiled/special/runtime.hpp"
#include "performance_profile.hpp"
#include "../platform/compat/resource_mount.hpp"
#include "../engine/decoded_resource_cache.hpp"
#include <winrecomp/win32_state.hpp>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>

namespace s=wa2::special;namespace c=wa2::platform::compat;using wr::U32;
unsigned checks{},comparisons{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("check failed: " #x);}while(false)
struct Member {std::string name;std::vector<std::uint8_t> bytes;U32 size;bool compressed;};
struct Result {std::vector<std::uint8_t> bytes;std::array<std::uint8_t,4096> dictionary;U32 size{};std::vector<std::string> traces;};
std::vector<std::uint8_t> archive(const std::vector<Member>& members,unsigned padding){
    const auto count=padding+members.size();std::vector<std::uint8_t> out(16+44*count);std::memcpy(out.data(),"KCAP",4);
    const auto word=[&](std::size_t at,U32 value){for(unsigned n=0;n<4;++n)out[at+n]=std::uint8_t(value>>(8*n));};word(12,U32(count));
    for(std::size_t n=0;n<count;++n){const Member empty{"unused_"+std::to_string(n)+".dat",{17},1,false};const auto& item=n<padding?empty:members[n-padding];const auto at=16+n*44,offset=out.size(),size=item.bytes.size()+(item.compressed?8:0);word(at,item.compressed);std::memcpy(out.data()+at+4,item.name.data(),item.name.size());word(at+36,U32(offset));word(at+40,U32(size));out.resize(offset+size);
        if(item.compressed){word(offset,U32(size));word(offset+4,item.size);}std::copy(item.bytes.begin(),item.bytes.end(),out.begin()+offset+(item.compressed?8:0));}
    return out;
}
struct Fixture {
    s::Runtime runtime;
    std::shared_ptr<wr::ReadOnlyFiles> mount;
    std::vector<std::string> traces;
    U32 pak{},output{},heap{};
    std::uint64_t opens{};
    Fixture(const std::filesystem::path& root,std::uint64_t size,bool linear,bool native,const std::string& filename="images.pak"):runtime([&]{wr::ProcessOptions o;o.data_root=root;o.prefer_linear_memory=linear;return o;}(),{}, {},false,false,{},native){
        auto& p=runtime.process();mount=std::make_shared<c::ResourceMount>(std::vector<wr::ReadOnlyFileInfo>{{filename,size,1}},[this,root,filename](std::string_view){++opens;return std::fopen((root/filename).string().c_str(),"rb");});c::install_resource_mount(p,mount);
        p.replace_api("kernel32.dll","OutputDebugStringA",1,[this](wr::Process& p){traces.push_back(p.read_string(p.argument(0)));return 0u;});p.load(s::initial_image());pak=p.put_string(filename);output=p.allocate_bytes(4096);heap=p.callback(p.resolve("kernel32.dll","GetProcessHeap"),{});
    }
    Result load(const Member& member){
        auto& p=runtime.process();const auto profile=s::image_resource_profile();std::array<std::uint8_t,4096> initial;for(unsigned n=0;n<initial.size();++n)initial[n]=std::uint8_t(n*13+7);p.memory.copy_in(profile.dictionary,initial);p.memory.store(output,0x12345678,32);const auto trace_start=traces.size();
        Result result;result.size=p.callback(profile.image_load,std::array<U32,3>{pak,p.put_string(member.name),output},false);CHECK(result.size==member.size);const auto pointer=p.memory.load(output,32);CHECK(pointer && pointer!=0x12345678);result.bytes.resize(result.size);p.memory.copy_out(pointer,result.bytes);p.memory.copy_out(profile.dictionary,result.dictionary);result.traces.assign(traces.begin()+trace_start,traces.end());
        p.memory.store(pointer,p.memory.load(pointer,8)^255u,8);CHECK(p.callback(p.resolve("kernel32.dll","HeapFree"),std::array<U32,3>{heap,0,pointer})==1);return result;
    }
    U32 find(const std::string& name){auto& p=runtime.process();return p.callback(s::image_resource_profile().image_check,std::array<U32,2>{pak,p.put_string(name)},false);}
};
int main(int argc,char** argv){try{
    CHECK(argc==2 || argc==3);std::vector<Member> members{{"panel.tga",std::vector<std::uint8_t>(4096,41),4096,false},{"tail.tga",{0,0xee,0xf0,0xf1,0xf0},6,true}};
    std::vector<std::uint8_t> packed;unsigned written{};constexpr unsigned size=32768;
    while(written<size){const auto at=packed.size();packed.push_back(0);for(unsigned n=0;n<8 && written<size;++n){if(!written || size-written<3){packed[at]|=std::uint8_t(1u<<n);packed.push_back(97);++written;}else{const auto from=(0xfee + written - 1)&4095,count=std::min(18u,size-written);packed.push_back(std::uint8_t(from));packed.push_back(std::uint8_t(((from>>4)&0xf0)|(count-3)));written+=count;}}}
    members.push_back({"menu.tga",packed,size,true});const auto bytes=archive(members,256);std::array<std::uint64_t,2> baseline_ns{},candidate_ns{},baseline_opens{},candidate_opens{},hits{};
    for(unsigned mode=0;mode<2;++mode){const auto root=std::filesystem::path(argv[1]).parent_path()/("sc-image-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(std::filesystem::create_directory(root));{std::ofstream file(root/"images.pak",std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());CHECK(file);}
        Fixture before(root,bytes.size(),bool(mode),false),after(root,bytes.size(),bool(mode),true);
        for(const auto& member:members)for(unsigned repeat=0;repeat<4;++repeat){const auto a=before.load(member),b=after.load(member);CHECK(a.size==b.size && a.bytes==b.bytes && a.dictionary==b.dictionary && a.traces==b.traces);++comparisons;}
        for(const auto& name:{"panel.tga","MENU.TGA","absent.tga"}){CHECK(before.find(name)==after.find(name));++comparisons;}
        const auto begin=std::chrono::steady_clock::now();for(unsigned n=0;n<16;++n)CHECK(before.find("menu.tga")==259);baseline_ns[mode]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
        const auto next=std::chrono::steady_clock::now();for(unsigned n=0;n<16;++n)CHECK(after.find("menu.tga")==259);candidate_ns[mode]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-next).count();baseline_opens[mode]=before.opens;candidate_opens[mode]=after.opens;
        const auto report=after.runtime.process().state().reports.at("native_image_resources")();const auto key=report.find("\"image_decoded_cache_hits\":");CHECK(key!=report.npos);hits[mode]=std::stoull(report.substr(key+27));CHECK(hits[mode]>=6 && candidate_opens[mode]<baseline_opens[mode]);
    }
    unsigned real_comparisons{};std::array<std::uint64_t,2> real_before_ns{},real_after_ns{};
    if(argc==3){const auto root=std::filesystem::path(argv[2]),path=root/"gcn.pak";const auto length=std::filesystem::file_size(path);std::ifstream file(path,std::ios::binary);std::array<std::uint8_t,16> header;CHECK(file.read(reinterpret_cast<char*>(header.data()),header.size()));
        const auto value=[](const std::uint8_t* data){return U32(data[0])|(U32(data[1])<<8)|(U32(data[2])<<16)|(U32(data[3])<<24);};CHECK(std::memcmp(header.data(),"KCAP",4)==0);const auto count=value(header.data()+12);CHECK(count && count<=65536);std::vector<std::uint8_t> index(count*44);CHECK(file.read(reinterpret_cast<char*>(index.data()),index.size()));std::vector<Member> actual;
        for(unsigned n=0;n<count;++n){const auto* at=index.data()+n*44;const auto end=std::find(at+4,at+28,0);const std::string name(reinterpret_cast<const char*>(at+4),std::size_t(end-at-4));if(name!="sys_00000.tga" && name!="sys_00100.tga")continue;const bool compressed=value(at)!=0;U32 size=value(at+40);if(compressed){file.seekg(value(at+36));std::array<std::uint8_t,8> words;CHECK(file.read(reinterpret_cast<char*>(words.data()),8));size=value(words.data()+4);}actual.push_back({name,{},size,compressed});}
        CHECK(actual.size()==2);for(unsigned mode=0;mode<2;++mode){Fixture before(root,length,bool(mode),false,"gcn.pak"),after(root,length,bool(mode),true,"gcn.pak");for(const auto& member:actual)for(unsigned repeat=0;repeat<3;++repeat){auto now=std::chrono::steady_clock::now();const auto a=before.load(member);real_before_ns[mode]+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-now).count();now=std::chrono::steady_clock::now();const auto b=after.load(member);real_after_ns[mode]+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-now).count();CHECK(a.size==b.size && a.bytes==b.bytes && a.dictionary==b.dictionary && a.traces==b.traces);++real_comparisons;}}
    }
    std::ofstream out(argv[1]);out<<"{\"passed\":true,\"checks\":"<<checks<<",\"source_image_comparisons\":"<<comparisons<<",\"baseline_index_ns\":["<<baseline_ns[0]<<','<<baseline_ns[1]<<"],\"candidate_index_ns\":["<<candidate_ns[0]<<','<<candidate_ns[1]<<"],\"baseline_archive_opens\":["<<baseline_opens[0]<<','<<baseline_opens[1]<<"],\"candidate_archive_opens\":["<<candidate_opens[0]<<','<<candidate_opens[1]<<"],\"decoded_reuse_hits\":["<<hits[0]<<','<<hits[1]<<"],\"real_ui_comparisons\":"<<real_comparisons<<",\"real_ui_baseline_ns\":["<<real_before_ns[0]<<','<<real_before_ns[1]<<"],\"real_ui_candidate_ns\":["<<real_after_ns[0]<<','<<real_after_ns[1]<<"],\"original_static_loader_used\":true,\"dictionary_and_trace_preserved\":true,\"auxiliary_pc_only\":true,\"game_launched\":false,\"screen_access\":false,\"android_gain_measured\":false}\n";CHECK(out);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
