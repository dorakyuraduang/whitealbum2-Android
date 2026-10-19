// Reference uses existing generated C++ only, with drawing recorded in owned
// memory. This executable is a test; it is never linked into the Android game.
#include "text_layout.hpp"
#include <winrecomp/compiled.hpp>
#include <bit>
#include <fstream>
#include <iostream>

bool code_page_1093(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1094(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1095(wr::Cpu&,wr::Memory&,std::uint64_t&);
bool code_page_1140(wr::Cpu&,wr::Memory&,std::uint64_t&);
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
unsigned read32(std::istream& input){std::array<unsigned char,4> b{};input.read(reinterpret_cast<char*>(b.data()),4);CHECK(input);return b[0]|unsigned(b[1])<<8|unsigned(b[2])<<16|unsigned(b[3])<<24;}
int main(int argc,char** argv){try{
    CHECK(argc==3);wr::Memory memory;std::ifstream data(argv[1],std::ios::binary);CHECK(data);
    const auto count=read32(data);
    for(unsigned n=0;n<count;++n){const auto at=read32(data),length=read32(data),permissions=read32(data);std::vector<std::uint8_t> bytes(length);
        data.read(reinterpret_cast<char*>(bytes.data()),bytes.size());CHECK(data);memory.map(at,length,permissions);memory.initialize(at,bytes);}
    for(auto page:{0x100000u,0x110000u,0x200000u,0x201000u,0x4d1000u,0xa82000u,0xb34000u,0xbc8000u})memory.map(page,4096,3);
    memory.store(0xbc8310,1,32);memory.store(0xa823b1,28,8);memory.store(0xa823b2,32,8);memory.store(0xa823b3,40,8);
    unsigned comparisons=0;
    for(const std::string raw:std::array<std::string,4>{"ABCD",std::string("\x90\xcb\x8d\xef",4),std::string("A\xb1" "B",3),"ABC\\nDEF"}){
        const auto native=wa2::text::layout(raw);std::vector<std::array<unsigned,9>> recorded;
        memory.copy_in(0x110000,{reinterpret_cast<const std::uint8_t*>(raw.c_str()),raw.size()+1});
        wr::Cpu cpu;cpu.eip=0x446500;cpu.r[wr::ESP]=0x201e00;memory.store(cpu.r[wr::ESP],0xbadf00d,32);
        const std::array<unsigned,26> args{0x100000,0,14,28,4,0,14,0,0,0,28,0x110000,
            0xffffffff,0,255,255,255,255,0,0,0,0,0,0,0,1};
        for(unsigned i=0;i<args.size();++i)memory.store(cpu.r[wr::ESP]+4+i*4,args[i],32);
        std::uint64_t budget=100000;unsigned operations=0;
        while(cpu.eip!=0x4477cd && cpu.eip!=0x447872 && cpu.eip!=0xbadf00d){
            CHECK(++operations<100000);
            if(cpu.eip==0x4462e0){std::array<unsigned,9> call{};for(unsigned i=0;i<call.size();++i)call[i]=memory.load(cpu.r[wr::ESP]+0x104+i*4,32);
                recorded.push_back(call);cpu.eip=wr::pop(cpu,memory);continue;}
            const auto pc=cpu.eip;bool translated=false;try{switch(cpu.eip>>12){case 0x445:translated=code_page_1093(cpu,memory,budget);break;
            case 0x446:translated=code_page_1094(cpu,memory,budget);break;case 0x447:translated=code_page_1095(cpu,memory,budget);break;
            case 0x474:translated=code_page_1140(cpu,memory,budget);break;}}catch(const std::exception& error){throw std::runtime_error("reference PC "+std::to_string(pc)+": "+error.what());}
            if(!translated)throw std::runtime_error("text reference target "+std::to_string(cpu.eip));
        }
        std::cout<<"Recorded "<<recorded.size()<<" glyphs; native "<<native.letters.size()<<'\n';CHECK(recorded.size()==native.letters.size());
        for(std::size_t n=0;n<recorded.size();++n){const auto& call=recorded[n];const auto& letter=native.letters[n];
            const auto value=std::uint16_t(call[0]);const auto swapped=value>255 && !(value>=0xffb1 && value<=0xffdd)?std::uint16_t((value<<8)|(value>>8)):value;
            std::cout<<n<<" code "<<std::hex<<swapped<<std::dec<<" reference "<<int(call[1])<<','<<int(call[2])<<'-'<<int(call[3])<<','<<int(call[4])
                <<" source "<<call[5]<<'+'<<call[6]<<" atlas "<<call[8]<<" native "<<letter.x<<','<<letter.y<<'\n';
            CHECK(swapped==letter.code && int(call[1])==int(letter.x)+4 && int(call[2])==int(letter.y)+4);
            CHECK(int(call[3])-int(call[1])==32 && int(call[4])-int(call[2])==32 && call[5]==4 && call[6]==32 && call[8]==0);++comparisons;
        }
    }
    std::ofstream result(argv[2]);result<<"{\"schema\":\"wa2.native-text-reference.v1\",\"glyph_geometry_comparisons\":"<<comparisons
        <<",\"original_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"passed\":true}\n";
    std::cout<<"PASS existing generated-text geometry comparisons: "<<comparisons<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
