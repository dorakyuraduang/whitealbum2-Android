#include "font_glyphs.hpp"
#include <winrecomp/compiled.hpp>
#include <fstream>
#include <iostream>
#include <set>

bool code_page_1025(wr::Cpu&,wr::Memory&,std::uint64_t&);
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
unsigned short read16(std::ifstream& input){unsigned char b[2]{};input.read(reinterpret_cast<char*>(b),2);CHECK(input);return unsigned(b[0])|(unsigned(b[1])<<8);}
int main(int argc,char** argv){try{
    if(argc!=4)throw std::runtime_error("usage: native_fonts existing-page.bin font-ranges.bin report.json");
    wr::Memory memory;memory.map(0x401000,4096,5);memory.map(0x4a1000,4096,3);memory.map(0x200000,4096,3);
    std::array<std::uint8_t,4096> page{};std::ifstream code(argv[1],std::ios::binary);code.read(reinterpret_cast<char*>(page.data()),page.size());CHECK(code);memory.initialize(0x401000,page);
    std::ifstream ranges(argv[2],std::ios::binary);std::array<char,4> magic{};ranges.read(magic.data(),4);CHECK((magic==std::array<char,4>{'W','2','F','R'}));
    const std::array<unsigned,2> counts{read16(ranges),read16(ranges)};std::array<std::set<unsigned>,2> codes;
    for(unsigned mode=0;mode<2;++mode){const auto base=mode?0x4a14b8u:0x4a1420u;unsigned total=0;
        for(unsigned i=0;i<counts[mode];++i){const auto first=read16(ranges),last=read16(ranges);CHECK(first<=last);
            memory.store(base+i*4,first,16);memory.store(base+i*4+2,last,16);total+=last-first+1;
            for(auto point:{unsigned(first),unsigned(last)})for(int delta=-1;delta<=1;++delta){const auto value=int(point)+delta;if(value>=0 && value<=65535)codes[mode].insert(unsigned(value));}
        }
        memory.store(base+counts[mode]*4,0xffff,16);memory.store(base+counts[mode]*4+2,0xffff,16);
        CHECK(wa2::text::glyph_count(mode!=0)==total);
        for(auto point:{0u,0x20u,0x7eu,0xa1u,0xb1u,0xffb1u,0xffffu})codes[mode].insert(point);
        std::uint32_t random=17;for(unsigned i=0;i<64;++i){random=random*1664525+1013904223;codes[mode].insert(random>>16);}
    }
    unsigned comparisons=0;
    for(unsigned mode=0;mode<2;++mode)for(auto point:codes[mode]){
        wr::Cpu cpu;cpu.eip=0x401050;cpu.r[wr::ESP]=0x200f00;memory.store(cpu.r[wr::ESP],0x0badf00d,32);
        memory.store(cpu.r[wr::ESP]+4,point,32);memory.store(cpu.r[wr::ESP]+8,mode,32);std::uint64_t budget=10000;unsigned instructions=0;
        const auto scope=memory.execution_scope();
        while(cpu.eip!=0x0badf00d){CHECK(++instructions<10000);CHECK(code_page_1025(cpu,memory,budget));}
        CHECK(cpu.r[wr::EAX]==wa2::text::glyph_index(std::uint16_t(point),mode!=0));
        const auto box=wa2::text::glyph_box(std::uint16_t(point),mode!=0,40,2,36);const auto index=cpu.r[wr::EAX];
        CHECK(box.x==index%80*40+2 && box.y==index/80*40+2 && box.width==36 && box.height==36);++comparisons;
    }
    std::ofstream report(argv[3]);report<<"{\"schema\":\"wa2.native-fonts.v1\",\"generated_code_comparisons\":"<<comparisons
        <<",\"base_glyphs\":"<<wa2::text::glyph_count(false)<<",\"extended_glyphs\":"<<wa2::text::glyph_count(true)
        <<",\"source_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"whole_text_layout_ported\":false,\"passed\":true}\n";
    report.close();CHECK(report);std::cout<<"PASS native glyph lookup: "<<comparisons<<" existing-code comparisons; no source EXE or screen access.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
