// Real cached Main/Special sort regions against the shared prefix.
// Constructed RAM only. Special stops at the first following store; Main's
// drawing-kind guard skips every object and an unmapped device stops its tail.
#include "../recompiled/special/runtime.hpp"
#include "../recompiled/process.hpp"
#include "../recompiled/source_sort.hpp"
#include "sort_profile.hpp"
#include <bit>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check at ")+std::to_string(__LINE__)+": " #x);}while(false)
namespace a=wa2::aot;namespace s=wa2::special;using wr::U32;
struct Result {wr::Cpu cpu;std::vector<std::uint8_t> frame,objects;wr::FaultKind kind{};U32 address{};bool fault{};};
void same(const Result& x,const Result& y){
    CHECK(x.cpu.r==y.cpu.r && x.cpu.eip==y.cpu.eip && x.cpu.flags==y.cpu.flags && x.cpu.defined_flags==y.cpu.defined_flags);
    CHECK(x.cpu.fp.bytes==y.cpu.fp.bytes && x.cpu.selectors==y.cpu.selectors && x.cpu.fs_base==y.cpu.fs_base && x.cpu.gs_base==y.cpu.gs_base && x.cpu.fs_valid==y.cpu.fs_valid && x.cpu.gs_valid==y.cpu.gs_valid);
    CHECK(x.fault==y.fault && x.kind==y.kind && x.address==y.address && x.frame==y.frame && x.objects==y.objects);
}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned main_cases{},special_cases{},declines{};
    for(bool main_profile:{false,true})for(bool linear:{false,true}){
        wr::ProcessOptions options;options.prefer_linear_memory=linear;
        std::unique_ptr<a::Runtime> main;std::unique_ptr<s::Runtime> special;
        if(main_profile){main=std::make_unique<a::Runtime>(options);main->load();}
        else{special=std::make_unique<s::Runtime>(options);special->process().load(s::initial_image());}
        auto& p=main_profile?main->process():special->process();auto& m=p.memory;
        const auto source=main_profile?a::main_sort_source:s::sorting_source;
        if(main_profile)m.store(0xb34438,0,32);
        const U32 frame_at=0x21001000,frame_size=0x4000;
        m.map(frame_at,frame_size,wr::Memory::Read|wr::Memory::Write);
        const U32 frame_register=main_profile?frame_at+0x100:frame_at-source.pointer;
        const auto at=[&](std::int32_t offset){return U32(std::int64_t(frame_register)+offset);};
        const auto object_at=source.objects-1;
        const auto mask_page=source.mask&~4095u,mask_permissions=m.permissions(mask_page);
        m.protect(mask_page,4096,wr::Memory::Read|wr::Memory::Write);m.store(source.mask,1,32);m.protect(mask_page,4096,mask_permissions);
        a::Program main_program(m);a::StaticProgram special_program(m,s::functions(),s::locate);
        std::vector<std::uint8_t> objects(2048*256),frame(frame_size);
        std::mt19937 random(0x534f5254);
        const auto execute=[&](unsigned count,unsigned order,unsigned first,bool optimized){
            std::fill(objects.begin(),objects.end(),0);std::fill(frame.begin(),frame.end(),0xa5);
            const auto put=[&]<class T>(std::size_t offset,T value){std::memcpy(objects.data()+offset,&value,sizeof(value));};
            for(unsigned n=0;n<count && n<2048-first;++n){const auto index=first+(n*193)%(2048-first),offset=index*256;
                objects[offset]=1;objects[offset+1]=1;objects[offset+0x14]=0xff;
                const int key=order==0?int(n)-32:order==1?32-int(n):order==2?5:
                    std::array{-32768,-1,29999,30000,32767}[n%5];
                put(offset+2,std::int16_t(key));put(offset+4,U32(n%7==6?2:1));
            }
            m.copy_in(object_at,objects);m.copy_in(frame_at,frame);
            std::array<std::uint16_t,2048> keys,ids;keys.fill(30000);ids.fill(0xffff);
            m.copy_in(at(source.keys),{reinterpret_cast<const std::uint8_t*>(keys.data()),sizeof(keys)});
            m.copy_in(at(source.ids),{reinterpret_cast<const std::uint8_t*>(ids.data()),sizeof(ids)});
            wr::Cpu cpu=p.cpu;for(unsigned n=0;n<cpu.r.size();++n)cpu.r[n]=0x12345000+n*0x1111;
            cpu.r[source.frame_register]=frame_register;cpu.r[wr::EAX]=source.objects+first*256;cpu.r[wr::EBX]=first-1;
            if(!main_profile)cpu.r[wr::ESP]=frame_register-0x2438;
            cpu.eip=source.begin;cpu.flags=0x202|((first&1)?wr::CF:0);cpu.defined_flags=wr::STATUS_FLAGS;
            cpu.fp=wr::FpState{};cpu.fp.put16(0,0x7f);
            m.store(at(source.pointer),cpu.r[wr::EAX],32);
            std::optional<a::SourceSortReferenceScope> reference;if(!optimized)reference.emplace();
            Result result{};
            try{for(unsigned boundaries=0;;++boundaries){CHECK(boundaries<32);
                CHECK(main_profile?main_program.invoke_unmetered(cpu):special_program.invoke(cpu));
            }}catch(const wr::GuestFault& fault){result.fault=true;result.kind=fault.kind;result.address=fault.address;}
            CHECK(result.fault && result.kind==wr::FaultKind::memory);
            CHECK(main_profile?result.address==0:result.address==frame_at-4);
            result.cpu=cpu;result.frame.resize(frame_size);result.objects.resize(objects.size());
            m.copy_out(frame_at,result.frame);m.copy_out(object_at,result.objects);CHECK(result.objects==objects);
            return result;
        };
        for(unsigned count:{0u,1u,8u,32u,96u})for(unsigned order=0;order<4;++order)for(unsigned first:{0u,17u,1024u,2047u}){
            const auto original=execute(count,order,first,false),candidate=execute(count,order,first,true);
            try{same(original,candidate);}catch(...){std::cerr<<"profile "<<main_profile<<" linear "<<linear<<" count "<<count<<" order "<<order<<" first "<<first<<'\n';throw;}
            if(main_profile)++main_cases;else ++special_cases;
        }
        // Direct decline must leave even unrelated frame/object bytes alone.
        for(unsigned guard=0;guard<5;++guard){
            execute(8,2,0,true);wr::Cpu cpu=p.cpu;cpu.eip=source.begin;cpu.r[source.frame_register]=frame_register;
            cpu.r[wr::EAX]=source.objects;cpu.r[wr::EBX]=0xffffffff;
            m.store(at(source.pointer),source.objects,32);
            std::array<std::uint16_t,2048> keys;keys.fill(30000);m.copy_in(at(source.keys),{reinterpret_cast<const std::uint8_t*>(keys.data()),sizeof(keys)});
            if(guard==0){m.store(at(source.keys),2,16);m.store(at(source.keys)+2,1,16);}
            if(guard==1)m.store(at(source.pointer),0,32);
            if(guard==2)cpu.eip=source.end;
            if(guard==3)cpu.r[source.frame_register]=1;
            const auto protected_page=at(source.keys)&~4095u;
            if(guard==4)m.protect(protected_page,4096,wr::Memory::Read|wr::Memory::Write|wr::Memory::Execute);
            const auto before=cpu;std::vector<std::uint8_t> old_frame(frame_size),old_objects(objects.size());m.copy_out(frame_at,old_frame);m.copy_out(object_at,old_objects);
            CHECK(!a::source_sort_full(cpu,m,source));
            std::vector<std::uint8_t> new_frame(frame_size),new_objects(objects.size());m.copy_out(frame_at,new_frame);m.copy_out(object_at,new_objects);
            CHECK(cpu.r==before.r && cpu.eip==before.eip && cpu.flags==before.flags && cpu.fp.bytes==before.fp.bytes && old_frame==new_frame && old_objects==new_objects);
            if(guard==4)m.protect(protected_page,4096,wr::Memory::Read|wr::Memory::Write);++declines;
        }
        CHECK(p.executed_instructions==0);
    }
    CHECK(main_cases==160 && special_cases==160 && declines==20);
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.shared-source-sort.v1\",\"main_source_cases\":"<<main_cases<<",\"special_source_cases\":"<<special_cases
        <<",\"decline_cases\":"<<declines<<",\"exact_cpu_fp_flags_arrays_pointer_and_fault_prefix\":true,\"duplicate_signed_and_sentinel_order\":true,\"paged_and_linear_memory\":true,"
          "\"same_main_special_implementation\":true,\"game_launched\":false,\"screen_access\":false,\"window_or_device_created\":false,\"passed\":true}\n";
    CHECK(report);std::cout<<"PASS shared Main/Special source sort: "<<main_cases+special_cases<<" exact cases; no game or device.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
