// Synthetic archives, caller-owned RAM and allocator hooks only; no game run.
#include "process.hpp"
#include "../port/native_resources.hpp"
#include "../platform/compat/resource_mount.hpp"
#include "../engine/resource_archive.hpp"
#include "file_io.hpp"
#include <winrecomp/win32_state.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed at ")+std::to_string(__LINE__)+": "+#x);}while(false)
using namespace wr;
namespace r=wa2::resource;
void put(std::vector<std::uint8_t>& data,std::size_t at,U32 value){for(unsigned n=0;n<4;++n)data[at+n]=std::uint8_t(value>>(n*8));}
struct Sample {std::vector<std::uint8_t> packed,expected;};
Sample sample(unsigned size,unsigned variant){
    Sample s;std::array<std::uint8_t,4096> ring{};unsigned position=0xfee,token=8;std::size_t flag{};std::mt19937 random(17+variant);
    auto begin=[&]{if(token==8){flag=s.packed.size();s.packed.push_back(0);token=0;}};
    while(s.expected.size()<size){begin();const auto left=size-s.expected.size();
        if(!variant || left<3 || (variant==2 && random()%3==0) || (variant==1 && s.expected.empty())){
            s.packed[flag]|=1u<<token++;const auto byte=std::uint8_t(random());s.packed.push_back(byte);s.expected.push_back(byte);ring[position]=byte;position=(position+1)&4095;
        }else{++token;const unsigned distance=variant==1?1:random()%4096,offset=(position-distance)&4095,length=std::min(unsigned(left),variant==1?18u:3+unsigned(random()%16));
            s.packed.push_back(std::uint8_t(offset));s.packed.push_back(std::uint8_t(((offset>>4)&0xf0)|(length-3)));
            for(unsigned n=0;n<length;++n){const auto byte=ring[(offset+n)&4095];s.expected.push_back(byte);ring[position]=byte;position=(position+1)&4095;}}
    }return s;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned exact{},failures{},aliases{},mapping_changes{};std::array<std::array<std::uint64_t,5>,3> timing{};
    std::vector<Sample> samples;for(unsigned size:{0,1,3,17,4095,4096,65537})for(unsigned kind=0;kind<3;++kind)samples.push_back(sample(size,kind));
    for(unsigned kind=0;kind<3;++kind)samples.push_back(sample(2u*1024*1024,kind));
    const auto root=std::filesystem::path(argv[1]).parent_path()/("resource-load-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(std::filesystem::create_directory(root));
    for(bool linear:{false,true}){
        ProcessOptions options;options.prefer_linear_memory=linear;options.data_root=root;
        Process p(FunctionExecutor{[](Cpu&,Memory&){throw std::runtime_error("unexpected source execution");return false;}},options);install_wa2_native_resources(p);
        U32 output{},source{},target{},manager{},record{},table{},blocker{};
        U32 allocations{},frees{},allocated_size{},returned{};bool change_mapping{},change_input{};
        p.replace_api("kernel32.dll","HeapAlloc",3,[&](Process& q){++allocations;CHECK(q.argument(1)==0);allocated_size=q.argument(2);
            if(change_mapping)q.memory.protect(blocker,4096,Memory::Write);
            if(change_input)q.memory.store(source+4,0xff,8);return returned;});
        p.replace_api("kernel32.dll","HeapFree",3,[&](Process& q){++frees;CHECK(q.argument(2)==returned);return 1u;});
        p.load(wa2::aot::initial_image());output=p.allocate_bytes(4096);source=p.allocate_bytes(3u*1024*1024);target=p.allocate_bytes(2u*1024*1024+4096);
        manager=p.allocate_bytes(4096);record=p.allocate_bytes(4096);table=p.allocate_bytes(4096);blocker=p.allocate_bytes(4096);returned=target;
        auto decode=[&](const Sample& s){p.memory.store(source,U32(s.expected.size()),32);p.memory.copy_in(source+4,s.packed);return p.callback(0x457010,std::array<U32,3>{U32(s.packed.size()),source,output});};
        auto check_output=[&](const Sample& s){CHECK(p.memory.load(output,32)==target && allocated_size==s.expected.size()+1);std::vector<std::uint8_t> actual(s.expected.size());p.memory.copy_out(target,actual);CHECK(actual==s.expected && p.memory.load(target+U32(actual.size()),8)==0);};
        for(const auto& s:samples){p.memory.store(target+U32(s.expected.size()),0x5a,8);CHECK(decode(s)==s.expected.size());check_output(s);++exact;}
        // Input and output-slot aliasing are safe because compressed input is
        // snapshotted before allocator callbacks and publication.
        change_input=true;CHECK(decode(samples[16])==samples[16].expected.size());check_output(samples[16]);change_input=false;++aliases;
        {const auto& s=samples[13];p.memory.store(source,U32(s.expected.size()),32);p.memory.copy_in(source+4,s.packed);
            CHECK(p.callback(0x457010,std::array<U32,3>{U32(s.packed.size()),source,source})==s.expected.size());CHECK(p.memory.load(source,32)==target);++aliases;}
        {const auto& s=samples[16];returned=source;CHECK(decode(s)==s.expected.size());CHECK(p.memory.load(output,32)==source);
            std::vector<std::uint8_t> actual(s.expected.size());p.memory.copy_out(source,actual);CHECK(actual==s.expected && p.memory.load(source+U32(actual.size()),8)==0);returned=target;++aliases;}
        {const auto& s=samples[16];p.memory.store(source,U32(s.expected.size()),32);p.memory.copy_in(source+4,s.packed);
            CHECK(p.callback(0x457010,std::array<U32,3>{U32(s.packed.size()),source,target+5})==s.expected.size());auto expected=s.expected;
            for(unsigned n=0;n<4;++n)expected[5+n]=std::uint8_t(target>>(n*8));std::vector<std::uint8_t> actual(expected.size());p.memory.copy_out(target,actual);CHECK(actual==expected);++aliases;}
        change_mapping=true;CHECK(decode(samples[15])==samples[15].expected.size());check_output(samples[15]);change_mapping=false;p.memory.protect(blocker,4096,Memory::Read|Memory::Write);++mapping_changes;
        for(unsigned kind=0;kind<6;++kind){const auto calls=allocations,free_before=frees;p.memory.store(output,0x51515151,32);auto bad=sample(19,0);
            if(kind==0)bad.packed.pop_back();if(kind==1)bad.packed.push_back(0);if(kind==2){bad.expected.resize(2);bad.packed={0,0,15};}
            if(kind==4){bad.expected.clear();bad.packed={0};}
            if(kind==5)returned=0;
            U32 result{};
            if(kind==3){p.memory.store(source,128u*1024*1024+1,32);result=p.callback(0x457010,std::array<U32,3>{0,source,output});}
            else result=decode(bad);
            CHECK(result==0 && p.memory.load(output,32)==0);CHECK(allocations==calls+(kind==5) && frees==free_before);returned=target;++failures;
        }
        // A late unwritable destination page leaves the whole decoded prefix
        // untouched and frees the unpublished allocation, matching copy_in.
        {const auto& s=samples[18];std::vector<std::uint8_t> before(s.expected.size(),0x7b),after(before.size());p.memory.copy_in(target,before);p.memory.protect(target+4096,4096,Memory::Read);
            const auto calls=frees;const auto saved=p.cpu;bool fault{};try{decode(s);}catch(const GuestFault& e){fault=e.kind==FaultKind::memory && e.address==target+4096;}CHECK(fault && frees==calls+1);p.cpu=saved;
            p.memory.copy_out(target,after);CHECK(after==before);p.memory.protect(target+4096,4096,Memory::Read|Memory::Write);++failures;}
        {const auto& s=samples[16];p.memory.protect(target+4096,4096,Memory::Read);p.memory.store(output,0x51515151,32);
            const auto calls=frees,saved_alloc=allocations;const auto saved=p.cpu;bool fault{};try{decode(s);}catch(const GuestFault& e){fault=e.kind==FaultKind::memory && e.address==target+4096;}CHECK(fault && frees==calls+1 && allocations==saved_alloc+1 && p.memory.load(output,32)==0x51515151);p.cpu=saved;
            std::vector<std::uint8_t> actual(s.expected.size());p.memory.copy_out(target,actual);CHECK(actual==s.expected);p.memory.protect(target+4096,4096,Memory::Read|Memory::Write);++failures;}
        {p.memory.protect(output,4096,Memory::Read);const auto calls=allocations;const auto saved=p.cpu;bool fault{};
            try{decode(samples[16]);}catch(const GuestFault& e){fault=e.kind==FaultKind::memory && e.address==output;}CHECK(fault && allocations==calls);p.cpu=saved;p.memory.protect(output,4096,Memory::Read|Memory::Write);++failures;}
        p.memory.store(manager+12,record,32);p.memory.store(manager+16,record+0x148,32);p.memory.store(record+4,1,32);p.memory.store(record+0x110,table,32);
        // Both archive layouts use this loader's zero dictionary, including
        // the KCAP case whose image loader would instead use space bytes.
        for(bool kcap:{false,true})for(unsigned kind=0;kind<5;++kind){const auto s=sample(65537,kind%3);const bool packed=kind!=3;const auto& payload=packed?s.packed:s.expected;
            const unsigned offset=kcap?60:48,headers=packed?(kcap?8:4):0;std::vector<std::uint8_t> archive(offset+headers+payload.size());
            std::memcpy(archive.data(),kcap?"KCAP":"LAC\0",4);put(archive,kcap?12:4,1);
            if(kcap){put(archive,16,packed);std::memcpy(archive.data()+20,"test.dat",8);put(archive,52,offset);put(archive,56,U32(headers+payload.size()));}
            else{for(unsigned n=0;n<8;++n)archive[8+n]=std::uint8_t("test.dat"[n])^255;archive[39]=packed;put(archive,40,U32(headers+payload.size()));put(archive,44,offset);}
            if(headers){if(kcap)put(archive,offset,U32(headers+payload.size()));put(archive,offset+(kcap?4:0),U32(s.expected.size()));}
            std::copy(payload.begin(),payload.end(),archive.begin()+offset+headers);if(kind==4)archive.back()=0; // trailing byte below guarantees rejection
            if(kind==4){archive.push_back(0);put(archive,kcap?56:40,U32(headers+payload.size()+1));if(kcap)put(archive,offset,U32(headers+payload.size()+1));}
            const auto path=root/(std::to_string(linear)+"-"+std::to_string(kcap)+"-"+std::to_string(kind)+".pak");{std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(archive.data()),std::streamsize(archive.size()));CHECK(f.good());}
            const auto name=p.put_string(path.filename().string());const auto handle=p.callback(p.resolve("kernel32.dll","CreateFileA"),std::array<U32,7>{name,0x80000000u,3,0,3,0x80,0});CHECK(handle!=0xffffffffu);
            const auto member=r::Archive::open(wa2::platform::open_resource_file(path)).members().front();
            p.memory.store(record,handle,32);std::array<std::uint8_t,40> legacy{};std::memcpy(legacy.data(),"test.dat",8);legacy[31]=packed;
            for(unsigned n=0;n<4;++n){legacy[32+n]=std::uint8_t(member.stored_size>>(n*8));legacy[36+n]=std::uint8_t(member.offset>>(n*8));}p.memory.copy_in(table,legacy);
            auto* stream=p.state().files.at(handle).stream;CHECK(std::fseek(stream,7,SEEK_SET)==0);p.cpu.r[ECX]=manager;const auto calls=allocations;const auto size=p.callback(0x458050,std::array<U32,3>{0,0,output});
            if(kind==4){CHECK(!size && p.memory.load(output,32)==0 && std::ftell(stream)==7 && allocations==calls);++failures;}
            else{CHECK(size==s.expected.size());check_output(s);CHECK(std::uint64_t(std::ftell(stream))==member.offset+member.stored_size);++exact;}
            CHECK(p.callback(p.resolve("kernel32.dll","CloseHandle"),std::array<U32,1>{handle}));
        }
        if(linear){for(unsigned kind=0;kind<3;++kind){const auto& s=samples[21+kind];
            for(unsigned round=0;round<5;++round){const auto begin=std::chrono::steady_clock::now();for(unsigned repeat=0;repeat<2;++repeat)CHECK(decode(s)==s.expected.size());
                timing[kind][round]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();check_output(s);}}}
        CHECK(!p.executed_instructions);
    }
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.general-resource-loading.v1\",\"exact_output_cases\":"<<exact<<",\"unchanged_failure_cases\":"<<failures<<",\"snapshot_alias_cases\":"<<aliases<<",\"mapping_change_cases\":"<<mapping_changes<<",\"timings\":[";
    for(unsigned kind=0;kind<3;++kind){if(kind)out<<',';out<<"{\"kind\":"<<kind<<",\"expanded_bytes\":2097152,\"repetitions\":2,\"ns\":[";for(unsigned round=0;round<5;++round){if(round)out<<',';out<<timing[kind][round];}out<<"]}";}
    out<<"],\"paged_and_contiguous_memory\":true,\"original_resources_opened\":false,\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS general resource loads: "<<exact<<" exact outputs, "<<failures<<" failures, "<<aliases<<" snapshot aliases.\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
