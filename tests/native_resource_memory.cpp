#include "process.hpp"
#include "../port/native_resources.hpp"
#include "../platform/compat/resource_mount.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
using namespace wr;
constexpr U32 dictionary=0xa7cdd0;
unsigned reference(std::span<const std::uint8_t> input,std::span<std::uint8_t> output,std::array<std::uint8_t,4096>& ring){
    unsigned flags{},position=0xfee;std::size_t at{},written{};
    while(written<output.size()){
        flags>>=1;if(!(flags&0x100)){CHECK(at<input.size());flags=input[at++]|0xff00;}
        if(flags&1){CHECK(at<input.size());const auto value=input[at++];output[written++]=value;ring[position]=value;position=(position+1)&4095;}
        else{CHECK(at+2<=input.size());const unsigned low=input[at++],high=input[at++],offset=low|((high&0xf0)<<4),length=(high&15)+3;CHECK(length<=output.size()-written);
            for(unsigned n=0;n<length;++n){const auto value=ring[(offset+n)&4095];output[written++]=value;ring[position]=value;position=(position+1)&4095;}}
    }CHECK(at==input.size());return flags>>1;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned checked{},rejections{},image_loads{};std::mt19937 random(0x437000);
    for(bool linear:{false,true}){
        const auto root=std::filesystem::path(argv[1]).parent_path()/("lzss-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));CHECK(std::filesystem::create_directory(root));
        for(bool packed:{false,true}){std::vector<std::uint8_t> bytes(packed?72:64);auto put=[&](unsigned at,U32 value){for(unsigned n=0;n<4;++n)bytes[at+n]=std::uint8_t(value>>(n*8));};
            std::memcpy(bytes.data(),"KCAP",4);put(12,1);put(16,packed?1:0);std::memcpy(bytes.data()+20,"Test.Dat",8);put(52,60);put(56,packed?12:4);
            if(packed){put(60,12);put(64,3);bytes[68]=7;bytes[69]='a';bytes[70]='b';bytes[71]='c';}else{bytes[60]='a';bytes[61]='b';bytes[62]=0;bytes[63]='c';}
            std::ofstream file(root/(packed?"compressed.bin":"raw.bin"),std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));CHECK(file.good());}
        ProcessOptions options;options.data_root=root;options.prefer_linear_memory=linear;Process p(FunctionExecutor{[](Cpu&,Memory&){return false;}},options);install_wa2_native_resources(p);
        auto mount=std::make_shared<wa2::platform::compat::ResourceMount>(std::vector<ReadOnlyFileInfo>{{"packed.pak",72,0},{"raw.pak",64,0}},[&](std::string_view name){return std::fopen((root/(name=="packed.pak"?"compressed.bin":"raw.bin")).string().c_str(),"rb");});
        wa2::platform::compat::install_resource_mount(p,mount);
        U32 last_trace{};unsigned traces{};p.replace_api("kernel32.dll","OutputDebugStringA",1,[&](Process& q){last_trace=q.argument(0);++traces;return 0u;});
        p.load(wa2::aot::initial_image());CHECK(bool(p.memory.execution_scope().linear_base())==linear);
        const auto input=p.allocate_bytes(65536),output=p.allocate_bytes(65536);std::array<std::uint8_t,4096> original_ring;
        for(unsigned variant=0;variant<36;++variant){
            std::vector<std::uint8_t> packed;std::size_t size{};const auto groups=variant?2+variant*2:0;
            for(unsigned g=0;g<groups;++g){const auto flags=std::uint8_t(variant%7==0?255:random());packed.push_back(flags);
                const unsigned count=g+1==groups?1+random()%8:8;
                for(unsigned n=0;n<count;++n){if(flags&(1u<<n)){packed.push_back(std::uint8_t(random()));++size;}
                    else{const unsigned distance=variant%5==0?(g+n)%19:random()%4096,offset=((0xfee +unsigned(size))-distance)&4095,length=3+random()%16;
                        packed.push_back(std::uint8_t(offset));packed.push_back(std::uint8_t(((offset>>4)&0xf0)|(length-3)));size+=length;}}
            }
            CHECK(size<65536);for(auto& byte:original_ring)byte=std::uint8_t(random());p.memory.copy_in(dictionary,original_ring);p.memory.copy_in(input,packed);
            std::vector<std::uint8_t> expected(size),actual(size);auto expected_ring=original_ring;std::fill_n(expected_ring.begin(),0xfee,32);const auto flags=reference(packed,expected,expected_ring);
            const auto result=p.callback(0x437000,std::array<U32,4>{output,input,U32(packed.size()),U32(size)},false);CHECK(result==size);
            p.memory.copy_out(output,actual);std::array<std::uint8_t,4096> actual_ring;p.memory.copy_out(dictionary,actual_ring);CHECK(actual==expected && actual_ring==expected_ring);
            CHECK(last_trace==(flags&0x100?0x4a40ec:0x4a40f0));++checked;
        }
        for(unsigned kind=0;kind<3;++kind){
            const std::array<std::uint8_t,3> malformed{0,0,0};p.memory.copy_in(input,malformed);p.memory.copy_in(dictionary,original_ring);
            std::array<std::uint8_t,32> before;before.fill(0x5a);p.memory.copy_in(output,before);
            if(kind==1)p.memory.protect(output,4096,Memory::Read);
            bool rejected=false;const auto count=traces;const auto saved=p.cpu;
            try{p.callback(0x437000,std::array<U32,4>{kind==2?dictionary:output,input,kind==0?1u:3u,kind==0?17u:3u},false);}
            catch(const GuestFault& fault){rejected=fault.kind==FaultKind::untranslated && fault.address==0x437000;}CHECK(rejected && traces==count);
            p.cpu=saved;
            if(kind==1)p.memory.protect(output,4096,Memory::Read|Memory::Write);
            std::array<std::uint8_t,32> after;p.memory.copy_out(output,after);CHECK(before==after);std::array<std::uint8_t,4096> ring;p.memory.copy_out(dictionary,ring);CHECK(ring==original_ring);++rejections;
        }
        const auto member=p.put_string("test.dat"),slot=p.allocate_bytes(4096);
        for(bool packed:{false,true}){const auto name=p.put_string(packed?"packed.pak":"raw.pak");p.memory.copy_in(dictionary,original_ring);
            CHECK(p.callback(0x437220,std::array<U32,2>{name,member},false)==1);
            const auto length=p.callback(0x437310,std::array<U32,3>{name,member,slot},false);CHECK(length==(packed?3:4));const auto pointer=p.memory.load(slot,32);CHECK(pointer);
            std::array<std::uint8_t,4> actual{};p.memory.copy_out(pointer,std::span(actual).first(length));CHECK(actual[0]=='a' && actual[1]=='b' && actual[packed?2:3]=='c');
            auto expected_ring=original_ring;if(packed){std::fill_n(expected_ring.begin(),0xfee,32);std::array<std::uint8_t,3> expanded{};reference(std::array<std::uint8_t,4>{7,'a','b','c'},expanded,expected_ring);}
            std::array<std::uint8_t,4096> ring;p.memory.copy_out(dictionary,ring);CHECK(ring==expected_ring);
            const auto heap=p.callback(p.resolve("kernel32.dll","GetProcessHeap"),{});CHECK(p.callback(p.resolve("kernel32.dll","HeapFree"),std::array<U32,3>{heap,0,pointer})==1);++image_loads;
        }
        CHECK(p.executed_instructions==0);
    }
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-resource-memory.v1\",\"decode_and_dictionary_cases\":"<<checked<<",\"unchanged_rejections\":"<<rejections<<",\"native_kcap_image_loads\":"<<image_loads<<",\"contiguous_and_paged_memory\":true,\"instruction_step_calls\":0,\"real_16k_page_device_test\":false,\"source_executable_opened\":false,\"original_resources_opened\":false,\"screen_access\":false,\"game_launched\":false,\"passed\":true}\n";
    std::cout<<"PASS native resource memory: "<<checked<<" source ABI decodes, paged/contiguous output and unchanged invalid-input rejection.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
