// Exact closed-loop comparison in invented RAM. No game, resources or devices.
#include "bodies.hpp"
#include "image_pixel_profile.hpp"
#include <winrecomp/compiled.hpp>
#include <array>
#include <fstream>
#include <iostream>
#include <random>
namespace a=wa2::aot;namespace s=wa2::special;using wr::U32;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("pixel check ")+std::to_string(__LINE__)+": " #x);}while(false)
constexpr U32 Data=0x1200000,Stack=0x2500000,Luts=0xa80000,Tables=0x2600000;
bool source_oracle(wr::Cpu& s,wr::Memory& m,std::uint64_t& budget){switch(s.eip){
#include "image_pixel_oracle.inc"
default:return false;}}
bool main_oracle(wr::Cpu& s,wr::Memory& m,std::uint64_t& budget){switch(s.eip){
#include "main_image_pixel_oracle.inc"
default:return false;}}
bool same(const wr::Cpu& x,const wr::Cpu& y){return x.r==y.r && x.eip==y.eip && x.flags==y.flags && x.defined_flags==y.defined_flags && x.fp.bytes==y.fp.bytes && x.selectors==y.selectors && x.fs_base==y.fs_base && x.gs_base==y.gs_base && x.fs_valid==y.fs_valid && x.gs_valid==y.gs_valid;}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},main_cases{},guards{},accepted{};
    for(bool main_profile:{false,true})for(bool linear:{false,true}){
        const auto& chroma_source=main_profile?a::main_image_chroma_source:s::image_chroma_source;
        const auto& colour_source=main_profile?a::main_image_colour_source:s::image_colour_source;
        wr::Memory m;if(linear)CHECK(m.enable_linear_memory());m.map(0x401000,0xa0000,3);
        for(const auto& f:main_profile?a::functions():s::functions())for(const auto& span:f.code)m.copy_in(span.address,span.bytes);
        m.protect(0x401000,0xa0000,5);m.map(Data,32768,3);m.map(Stack,8192,3);m.map(Luts,0x12000,3);m.map(Tables,4096,3);
        std::array<std::uint8_t,32768> data;std::array<std::uint8_t,128> stack;
        std::vector<std::uint8_t> luts(0x12000),tables(4096);
        const auto snapshot=[&]{std::vector<std::uint8_t> out(data.size()+stack.size()+luts.size()+tables.size());std::size_t at{};
            for(auto region:{std::pair{Data,data.size()},std::pair{Stack+4096-64,stack.size()},std::pair{Luts,luts.size()},std::pair{Tables,tables.size()}}){m.copy_out(region.first,std::span(out).subspan(at,region.second));at+=region.second;}return out;};
        for(bool chroma:{false,true})for(unsigned kind=0;kind<6;++kind)for(unsigned count:{63u,64u,65u,1025u})for(unsigned offset:{0u,4094u})for(unsigned seed:{1u,7u}){
            const auto step=kind<(chroma?4u:3u)?4u:3u;const bool grey=kind==0 || kind==1 || kind==4;
            std::mt19937 rng(seed+kind*71+count);for(auto& v:data)v=std::uint8_t(rng());for(auto& v:stack)v=std::uint8_t(rng());for(auto& v:luts)v=std::uint8_t(rng());for(auto& v:tables)v=std::uint8_t(rng());
            const auto first=Data+offset,frame=Stack+4096;const auto scaled=U32((seed==1?1:193)*257);
            wr::Cpu initial;for(auto& v:initial.r)v=rng();for(auto& v:initial.fp.bytes)v=std::uint8_t(rng());
            initial.flags=(rng()&wr::STATUS_FLAGS)|0x602;initial.defined_flags=rng()&wr::STATUS_FLAGS;initial.selectors={1,2,3,4,5,6};initial.fs_base=0x12345678;initial.gs_base=0x87654321;initial.fs_valid=initial.gs_valid=true;
            initial.r[wr::EBP]=frame;initial.r[wr::ESP]=frame-(main_profile?0:chroma && kind!=2 && kind!=3?12:16);
            if(chroma){initial.eip=chroma_source.entries[kind];initial.r[grey?wr::EDX:wr::ECX]=count;initial.r[step==4?wr::ESI:wr::EDI]=scaled;}
            else{initial.eip=colour_source.entries[kind];initial.r[wr::EBX]=!main_profile && kind%3==0?count:first;initial.r[wr::ECX]=Tables;
                initial.r[wr::EDX]=kind%3==0?(main_profile?count:Tables+256):count;initial.r[wr::EDI]=Tables+512;initial.r[wr::ESI]=kind%3==2?count:Tables+768;
                if(main_profile)initial.r[wr::EBP]=kind%3==0?Tables+256:count;}
            const auto prepare=[&]{m.copy_in(Data,data);m.copy_in(Stack+4096-64,stack);m.copy_in(Luts,luts);m.copy_in(Tables,tables);
                if(main_profile){if(chroma){m.store(frame+(!grey && step==4?0x18:0x10),first,32);if(!grey)m.store(frame+(step==4?0x10:0x08),Tables+1024,32);}}
                else{m.store(frame+8,first,32);m.store(frame-4,Tables+1024,32);}};
            prepare();auto actual=initial;const auto before=snapshot();const bool hit=chroma?a::native_image_chroma(actual,m,chroma_source):a::native_image_colours(actual,m,colour_source);
            if(count<64){CHECK(!hit && same(actual,initial) && snapshot()==before);++guards;continue;}
            CHECK(hit);++accepted;const auto native_bytes=snapshot();prepare();auto original=initial;std::uint64_t budget=10000000;
            for(unsigned n=0;original.eip!=actual.eip;++n){CHECK(n<100000 && (main_profile?main_oracle(original,m,budget):source_oracle(original,m,budget)));}
            if(!same(original,actual)){std::cerr<<"kind "<<kind<<" chroma "<<chroma<<" count "<<count<<" linear "<<linear<<'\n';for(unsigned r=0;r<8;++r)if(original.r[r]!=actual.r[r])std::cerr<<"r"<<r<<" "<<std::hex<<original.r[r]<<" / "<<actual.r[r]<<std::dec<<'\n';std::cerr<<"flags "<<original.flags<<" / "<<actual.flags<<" defined "<<original.defined_flags<<" / "<<actual.defined_flags<<'\n';}
            CHECK(same(original,actual));CHECK(snapshot()==native_bytes);if(main_profile)++main_cases;else ++cases;
        }
        if(main_profile)continue;
        // Transactional declines for aliases, executable output and overflow.
        for(unsigned kind:{0u,2u,5u})for(unsigned reason=0;reason<4;++reason){
            wr::Cpu cpu;cpu.eip=s::image_chroma_source.entries[kind];cpu.r[wr::ESP]=Stack+4096-16;cpu.r[wr::EBP]=Stack+4096;cpu.r[wr::EDX]=cpu.r[wr::ECX]=64;cpu.r[wr::ESI]=cpu.r[wr::EDI]=257;
            U32 at=reason==0?Stack+4096:reason==1?(kind?s::image_chroma_source.channel+257:Data+32768-100):reason==2?0x401000:0xfffffff0u;
            m.store(Stack+4104,at,32);m.store(Stack+4092,Tables+1024,32);const auto saved=cpu;const auto bytes=snapshot();
            CHECK(!a::native_image_chroma(cpu,m,s::image_chroma_source) && same(cpu,saved) && snapshot()==bytes);++guards;
        }
    }
    std::ofstream out(argv[1]);out<<"{\"passed\":true,\"source_loop_comparisons\":"<<cases<<",\"main_loop_comparisons\":"<<main_cases<<",\"accepted\":"<<accepted<<",\"unchanged_declines\":"<<guards<<",\"full_cpu_fp_ram_equal\":true,\"both_memory_modes\":true,\"shared_main_kernels\":true,\"game_launched\":false,\"screen_access\":false,\"auxiliary_pc_only\":true}\n";CHECK(out);std::cout<<"PASS SC/Main image pixels: "<<cases<<'/'<<main_cases<<" exact loops, "<<guards<<" declines\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
