#include "raster_image.hpp"
#include "resource_archive.hpp"
#include "file_io.hpp"
#include <winrecomp/compiled.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

bool code_page_1104(wr::Cpu&,wr::Memory&,std::uint64_t&);
namespace im=wa2::image;
using Bytes=std::vector<std::uint8_t>;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
struct Reference {
    wr::Memory memory;
    Reference(const std::filesystem::path& page){
        memory.map(0x100000,4096,3);memory.map(0x200000,4096,3);memory.map(0x300000,4096,3);memory.map(0xb92000,4096,3);
        memory.map(0x450000,4096,5);std::array<std::uint8_t,4096> data{};
        std::ifstream input(page,std::ios::binary);input.read(reinterpret_cast<char*>(data.data()),data.size());CHECK(input);memory.initialize(0x450000,data);
    }
    void compare(std::span<const std::uint8_t> source,bool swap){
        const auto info=im::inspect_game_tga(source,swap);
        memory.copy_in(0x300000,source.first(std::min<std::size_t>(4096,source.size())));
        wr::Cpu cpu;cpu.eip=0x450330;cpu.r[wr::ESP]=0x200f00;memory.store(cpu.r[wr::ESP],0x0badf00d,32);
        memory.store(cpu.r[wr::ESP]+4,0x300000,32);
        for(unsigned n=1;n<9;++n)memory.store(cpu.r[wr::ESP]+4+n*4,0x100000+n*4,32);
        memory.store(cpu.r[wr::ESP]+40,swap?1u:0u,32);std::uint64_t budget=10000;unsigned count=0;
        while(cpu.eip!=0x0badf00d){CHECK(++count<10000);if(!code_page_1104(cpu,memory,budget))throw std::runtime_error("image oracle target "+std::to_string(cpu.eip));}
        CHECK(cpu.r[wr::EAX]==0xb92620);
        CHECK(memory.load(0x100004,32)==0x300000+info.data_offset);
        CHECK(memory.load(0x100010,32)==info.width && memory.load(0x100014,32)==info.height);
        CHECK(memory.load(0x100018,32)==info.bits && memory.load(0x10001c,32)==unsigned(info.alpha));
        CHECK(memory.load(0x100020,32)==(info.top_origin?0x20u:0u));
        if(info.bits==8){
            CHECK(memory.load(0x10000c,32)==info.palette_entries);
            const auto palette=memory.load(0x100008,32);
            for(unsigned i=0;i<info.palette_entries;++i)for(unsigned c=0;c<4;++c)
                CHECK(memory.load(palette+i*4+c,8)==info.palette[i][c]);
        }
    }
};
Bytes header(unsigned bits,bool top,unsigned entries=0,unsigned palette_bits=0){
    Bytes data(18);data[2]=bits==8?1:2;data[5]=std::uint8_t(entries);data[6]=std::uint8_t(entries>>8);
    data[7]=std::uint8_t(palette_bits);data[12]=2;data[14]=2;data[16]=std::uint8_t(bits);data[17]=top?0x20:0;
    return data;
}
template<class F>void rejects(F function){bool rejected=false;try{function();}catch(const im::Error&){rejected=true;}CHECK(rejected);}
int main(int argc,char** argv){try{
    if(argc!=4)throw std::runtime_error("usage: native_images res generated-page.bin report.json");
    Reference reference(argv[2]);unsigned comparisons=0,checks=0,real_images=0;std::uint64_t decoded=0;
    const std::array<std::array<std::uint8_t,4>,4> colours{{{250,5,3,240},{1,200,7,160},{2,9,180,80},{11,12,13,0}}};
    for(auto bits:{24u,32u})for(auto top:{false,true}){
        auto data=header(bits,top);
        for(const auto& colour:colours){data.push_back(colour[2]);data.push_back(colour[1]);data.push_back(colour[0]);if(bits==32)data.push_back(colour[3]);}
        for(auto swap:{false,true}){reference.compare(data,swap);++comparisons;}
        const auto image=im::decode_game_tga(data);CHECK(image.width==2 && image.height==2 && image.rgba.size()==16);
        for(unsigned i=0;i<4;++i){const auto source=top?i:(1-i/2)*2+i%2;for(unsigned c=0;c<4;++c)
            CHECK(image.rgba[i*4+c]==(c==3 && bits==24?255:colours[source][c]));}
        const auto region=im::decode_game_tga_region(data,{1,0,1,2});CHECK(region.width==1 && region.height==2 && region.rgba.size()==8);
        for(unsigned y=0;y<2;++y)for(unsigned c=0;c<4;++c)CHECK(region.rgba[y*4+c]==image.rgba[(y*2+1)*4+c]);++checks;
        CHECK(im::decode_game_tga_region(data,{2,2,0,0}).rgba.empty());++checks;
        rejects([&]{im::decode_game_tga_region(data,{2,0,1,1});});++checks;
        ++checks;
        auto truncated=data;truncated.pop_back();rejects([&]{im::decode_game_tga(truncated);});++checks;
    }
    for(auto palette_bits:{24u,32u})for(auto top:{false,true}){
        auto data=header(8,top,4,palette_bits);
        for(const auto& colour:colours){data.push_back(colour[2]);data.push_back(colour[1]);data.push_back(colour[0]);if(palette_bits==32)data.push_back(colour[3]);}
        data.insert(data.end(),{0,1,2,3});
        for(auto swap:{false,true}){reference.compare(data,swap);++comparisons;}
        const auto image=im::decode_game_tga(data);
        for(unsigned i=0;i<4;++i){const auto source=top?i:(1-i/2)*2+i%2;for(unsigned c=0;c<4;++c)
            CHECK(image.rgba[i*4+c]==(c==3 && palette_bits==24?255:colours[source][c]));}
        const auto region=im::decode_game_tga_region(data,{1,0,1,2});
        for(unsigned y=0;y<2;++y)for(unsigned c=0;c<4;++c)CHECK(region.rgba[y*4+c]==image.rgba[(y*2+1)*4+c]);++checks;
        ++checks;auto invalid=data;invalid.back()=4;rejects([&]{im::decode_game_tga(invalid);});++checks;
    }
    for(unsigned size=0;size<18;++size){Bytes data(size);rejects([&]{im::decode_game_tga(data);});++checks;}
    for(auto kind:{9u,10u}){auto data=header(24,false);data[2]=std::uint8_t(kind);rejects([&]{im::decode_game_tga(data);});++checks;}
    auto overflow=header(32,false);overflow[12]=overflow[13]=overflow[14]=overflow[15]=255;rejects([&]{im::decode_game_tga(overflow);});++checks;
    for(const char* name:{"bak.pak","char.pak","grp.pak","fnt.pak","fon.pak"}){
        auto archive=wa2::resource::Archive::open(wa2::platform::open_resource_file(std::filesystem::path(argv[1])/name));
        auto found=wa2::resource::Archive::missing;
        for(std::size_t i=0;i<archive.members().size();++i){const auto& item=archive.members()[i];
            if(item.name.size()>=4 && wa2::resource::compare_names(std::string_view(item.name).substr(item.name.size()-4),".tga")==0 && item.stored_size){found=i;break;}}
        CHECK(found!=wa2::resource::Archive::missing);const auto data=archive.load(found);reference.compare(data,true);++comparisons;
        const bool font=std::string_view(name)=="fnt.pak" || std::string_view(name)=="fon.pak";
        const auto image=font?im::decode_game_tga_region(data,{0,0,24,24}):im::decode_game_tga(data);
        CHECK(image.width && image.height && image.rgba.size()==std::size_t(image.width)*image.height*4);
        decoded+=image.rgba.size();++real_images;
    }
    std::ofstream report(argv[3]);report<<"{\"schema\":\"wa2.native-images.v1\",\"generated_layout_palette_comparisons\":"<<comparisons
        <<",\"pixel_and_boundary_cases\":"<<checks<<",\"real_resource_images\":"<<real_images<<",\"real_rgba_bytes\":"<<decoded
        <<",\"original_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"renderer_integration_verified\":false,\"passed\":true}\n";
    report.close();CHECK(report);std::cout<<"PASS native game images: "<<comparisons<<" generated-code layout/palette checks, "<<checks<<" pixel/boundary cases, "<<real_images<<" actual archive images. No screen access.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
