#include "pixel_transfer.hpp"
#include "../platform/render/source_pixels.hpp"
#include <array>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
using wa2::image::PixelOrder;using wa2::image::transfer_32;
void expected(std::span<std::uint8_t> output,std::size_t dp,unsigned order,std::span<const std::uint8_t> input,std::size_t sp,unsigned source,unsigned w,unsigned h){
    const std::vector<std::uint8_t> original(input.begin(),input.end());
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const auto in=y*sp+x*4,out=y*dp+x*4;
        const auto red=original[in+((source&1)?2:0)],blue=original[in+((source&1)?0:2)],green=original[in+1];
        const auto alpha=(source&2)?255:original[in+3];
        output[out+((order&1)?2:0)]=red;output[out+1]=green;output[out+((order&1)?0:2)]=blue;output[out+3]=(order&2)?255:alpha;
    }
}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},invalid{},guest_rectangles{},guest_faults{};
    for(unsigned w:{1u,3u,17u,256u})for(unsigned h:{1u,3u})for(unsigned src=0;src<4;++src)for(unsigned dst=0;dst<4;++dst){
        const auto sp=w*4+4,dp=w*4+8;std::vector<std::uint8_t> input(sp*h),actual(dp*h,0xa5),reference=actual;
        for(unsigned n=0;n<input.size();++n)input[n]=std::uint8_t(n*31+7);
        expected(reference,dp,dst,input,sp,src,w,h);transfer_32(actual,dp,PixelOrder(dst),input,sp,PixelOrder(src),w,h);CHECK(actual==reference);++cases;
    }
    for(int offset:{-4,0,4})for(unsigned src=0;src<4;++src)for(unsigned dst=0;dst<4;++dst){
        std::vector<std::uint8_t> actual(256),reference;for(unsigned n=0;n<actual.size();++n)actual[n]=std::uint8_t(n*17+3);reference=actual;
        const auto source=64,destination=source+offset;
        expected(std::span(reference).subspan(destination),32,dst,std::span(reference).subspan(source),32,src,7,3);
        transfer_32(std::span(actual).subspan(destination),32,PixelOrder(dst),std::span(actual).subspan(source),32,PixelOrder(src),7,3);CHECK(actual==reference);++cases;
    }
    for(unsigned kind=0;kind<4;++kind){std::array<std::uint8_t,64> output;output.fill(0xa5);const auto before=output;std::array<std::uint8_t,64> input{};bool rejected{};
        try{transfer_32(std::span(output).first(kind==0?15:64),kind==1?7:16,PixelOrder::rgba8,std::span(input).first(kind==2?15:64),16,PixelOrder::bgra8,4,kind==3?0xffffffffu:2u);}
        catch(const std::invalid_argument&){rejected=true;}CHECK(rejected && output==before);++invalid;
    }
    for(bool linear:{false,true}){wr::Memory memory;if(linear)CHECK(memory.enable_linear_memory());memory.map(0x10000,12288,wr::Memory::Read|wr::Memory::Write);
        for(auto dimensions:{std::array<unsigned,4>{4090,20,12,7},std::array<unsigned,4>{64,16,16,7}}){
            const auto at=0x10000+dimensions[0],pitch=dimensions[1],row=dimensions[2],rows=dimensions[3];
            const auto dp=pitch==row?row:36u;
            std::vector<std::uint8_t> input(pitch*rows),output(dp*rows,0xa5),reference=output;for(unsigned n=0;n<input.size();++n)input[n]=std::uint8_t(n*19+11);memory.copy_in(at,input);
            for(unsigned y=0;y<rows;++y)std::copy_n(input.data()+y*pitch,row,reference.data()+y*dp);
            wa2::graphics::read_source_pixel_rows(memory,at,pitch,output,dp,row,rows);CHECK(output==reference);++guest_rectangles;
        }
        std::vector<std::uint8_t> output(96,0xa5);const auto before=output;memory.protect(0x11000,4096,0);bool rejected{};
        try{wa2::graphics::read_source_pixel_rows(memory,0x10ff0,16,output,32,16,3);}
        catch(const wr::GuestFault& error){rejected=error.kind==wr::FaultKind::memory;}CHECK(rejected && output==before);++guest_faults;
    }
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-pixel-transfer.v1\",\"pixel_rectangle_cases\":"<<cases<<",\"invalid_inputs_unchanged\":"<<invalid
        <<",\"source_memory_rectangles\":"<<guest_rectangles<<",\"late_source_faults_unchanged\":"<<guest_faults
        <<",\"alpha_and_padding_verified\":true,\"overlap_verified\":true,\"graphics_device_created\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS native pixel transfer: "<<cases<<" rectangles with independent byte/alpha/padding expectations.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
