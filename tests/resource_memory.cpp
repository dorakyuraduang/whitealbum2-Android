// Resource-sized allocations, including holes, partial commits and protection.
#include <winrecomp/memory.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace wr;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
int main(int argc,char** argv){try{
    CHECK(argc==3);Memory memory;CHECK(memory.enable_linear_memory());constexpr unsigned bytes=52992000,at=0x20000000,cycles=32;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned n=0;n<cycles;++n){memory.map(at,bytes,Memory::Read|Memory::Write);CHECK(memory.load(at,32)==0 && memory.load(at+bytes-4,32)==0);
        memory.store(at,n+1,32);memory.store(at+bytes-4,0x12345678,32);CHECK(memory.load(at,32)==n+1 && memory.load(at+bytes-4,32)==0x12345678);memory.release(at);CHECK(memory.regions().empty());}
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();const auto bulk=memory.native_operations();
    memory.reserve(at,16385);memory.commit(at+4096,4096,Memory::Read|Memory::Write);memory.store(at+4096,0x11223344,32);memory.protect(at+4096,4096,Memory::Read);
    memory.commit(at,16385,Memory::Read|Memory::Write);CHECK(memory.load(at+4096,32)==0x11223344 && memory.permissions(at+4096)==Memory::Read);
    CHECK(memory.load(at,32)==0 && memory.load(at+8192,32)==0 && memory.load(at+16384,8)==0);
    memory.decommit(at,16385);memory.commit(at,16385,Memory::Read|Memory::Write);CHECK(memory.load(at+4096,32)==0 && memory.load(at+16384,8)==0);memory.release(at);
    memory.map(at,4096,Memory::Read|Memory::Execute);memory.initialize(at,std::array<std::uint8_t,4>{1,2,3,4});CHECK(memory.load(at,32)==0x04030201);memory.release(at);
    const auto all=memory.native_operations();CHECK(all.committed_pages==all.decommitted_pages && memory.regions().empty());
    std::ofstream report(argv[2]);report<<"{\"schema\":\"wa2.resource-memory.v1\",\"resource_bytes\":"<<bytes<<",\"cycles\":"<<cycles<<",\"seconds\":"<<elapsed<<",\"large_commit_calls\":"<<bulk.commits<<",\"large_decommit_calls\":"<<bulk.decommits<<",\"large_committed_pages\":"<<bulk.committed_pages<<",\"all_pages_released\":true,\"zero_fill_and_partial_commit_preserved\":true,\"screen_access\":false,\"passed\":true}\n";CHECK(report);
    std::cout<<"PASS resource allocation: "<<cycles<<" x "<<bytes<<" bytes in "<<elapsed<<" s, "<<bulk.commits<<" commit / "<<bulk.decommits<<" decommit calls, partial commit/protection/zero fill preserved\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
