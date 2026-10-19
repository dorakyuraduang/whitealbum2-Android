// Original-resource-free CPU image transfer, crop, alpha and fault checks.
#include "../platform/render/source_pixels.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
using wa2::image::PixelOrder;
namespace g=wa2::graphics;
void image_upload_before(wr::Memory&,std::uint32_t,std::size_t,unsigned,PixelOrder,std::span<std::uint8_t>,std::size_t,PixelOrder,std::uint32_t,std::uint32_t);
constexpr std::uint32_t Source=0x10000;
void expected(std::span<std::uint8_t> output,std::size_t dp,unsigned order,std::span<const std::uint8_t> input,std::size_t sp,unsigned source,unsigned bytes,unsigned w,unsigned h){
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const auto in=y*sp+x*bytes,out=y*dp+x*4;
        const auto r=input[in+(bytes==3 || (source&1)?2:0)],b=input[in+(bytes==3 || (source&1)?0:2)],green=input[in+1];
        const auto a=bytes==3 || (source&2)?std::uint8_t(255):input[in+3];
        output[out+((order&1)?2:0)]=r;output[out+1]=green;output[out+((order&1)?0:2)]=b;output[out+3]=(order&2)?255:a;
    }
}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},partial_faults{},padding_cases{},declines{},overlaps{};
    for(bool linear:{false,true}){wr::Memory memory;if(linear)CHECK(memory.enable_linear_memory());memory.map(Source,65536,3);
        for(unsigned bytes:{3u,4u})for(unsigned src=0;src<(bytes==3?1:4);++src)for(unsigned dst=0;dst<4;++dst)for(unsigned w:{1u,3u,17u,257u})for(unsigned h:{1u,3u}){
            const auto sp=w*bytes+11,dp=w*4+17;const auto address=Source+4093;
            std::vector<std::uint8_t> input(sp*h),actual(dp*h,0x9d),reference=actual,old=actual;
            for(unsigned n=0;n<input.size();++n)input[n]=std::uint8_t(n*31+(n>>7)*13+7);memory.copy_in(address,input);
            expected(reference,dp,dst,input,sp,src,bytes,w,h);
            CHECK(g::transfer_source_pixels(memory,address,sp,bytes,PixelOrder(src),actual,dp,PixelOrder(dst),w,h));
            image_upload_before(memory,address,sp,bytes,PixelOrder(src),old,dp,PixelOrder(dst),w,h);
            CHECK(actual==reference && old==reference);++cases;
        }
        for(unsigned bytes:{3u,4u}){
            const auto row=bytes*4;const auto address=Source+4096-row;std::vector<std::uint8_t> input(256,73);memory.copy_in(address,input);
            memory.protect(Source+4096,4096,0);std::vector<std::uint8_t> output(64,0x9d),old=output;const auto before=output;
            bool rejected{},old_rejected{};std::uint32_t at{},old_at{};std::string message,old_message;
            try{g::transfer_source_pixels(memory,address,64,bytes,PixelOrder::bgra8,output,32,PixelOrder::rgba8,4,2);}
            catch(const wr::GuestFault& error){rejected=true;at=error.address;message=error.what();}
            try{image_upload_before(memory,address,64,bytes,PixelOrder::bgra8,old,32,PixelOrder::rgba8,4,2);}
            catch(const wr::GuestFault& error){old_rejected=true;old_at=error.address;old_message=error.what();}
            CHECK(rejected && old_rejected && at==old_at && message==old_message && output==before && old==before);memory.protect(Source+4096,4096,3);++partial_faults;
        }
        // Padding between rows is outside the source rectangle and may be unreadable.
        for(unsigned bytes:{3u,4u}){const auto row=bytes*4,address=Source+4096-row;std::array<std::uint8_t,16> data{};
            for(unsigned n=0;n<data.size();++n)data[n]=std::uint8_t(n*31+17);
            memory.copy_in(address,std::span(data).first(row));memory.copy_in(address+8192,std::span(data).first(row));memory.protect(Source+4096,4096,0);
            std::array<std::uint8_t,32> output{},reference{};std::vector<std::uint8_t> staged(8192+row);std::copy_n(data.begin(),row,staged.begin());std::copy_n(data.begin(),row,staged.begin()+8192);
            expected(reference,16,1,staged,8192,1,bytes,4,2);
            CHECK(g::transfer_source_pixels(memory,address,8192,bytes,PixelOrder::bgra8,output,16,PixelOrder::bgra8,4,2));CHECK(output==reference);
            memory.protect(Source+4096,4096,3);++padding_cases;
        }
        for(unsigned reason=0;reason<5;++reason){std::array<std::uint8_t,64> output;output.fill(0x9d);const auto before=output;
            CHECK(!g::transfer_source_pixels(memory,reason==0?0xfffffff8u:Source,16,reason==1?2:4,PixelOrder::rgba8,
                std::span(output).first(reason==2?15:64),reason==3?7:16,PixelOrder::rgba8,reason==4?0:4,2));CHECK(output==before);++declines;}
    }
    // The standalone 24-bit core retains snapshot semantics for overlapping buffers.
    for(int delta:{-4,0,4})for(unsigned dst=0;dst<4;++dst){std::vector<std::uint8_t> data(512);for(unsigned n=0;n<data.size();++n)data[n]=std::uint8_t(n*17+3);
        const auto before=data;auto reference=data;const auto source=64,destination=source+delta;
        expected(std::span(reference).subspan(destination),40,dst,std::span(before).subspan(source),28,1,3,7,3);
        wa2::image::transfer_bgr24(std::span(data).subspan(destination),40,PixelOrder(dst),std::span(data).subspan(source),28,7,3);CHECK(data==reference);++overlaps;}
    wr::Memory memory;CHECK(memory.enable_linear_memory());constexpr unsigned Width=1920,Height=1080,Repetitions=2;memory.map(0x1200000,Width*Height*4,3);
    std::vector<std::uint8_t> input(Width*Height*4);for(unsigned n=0;n<input.size();++n)input[n]=std::uint8_t(n*19+(n>>12)*7);memory.copy_in(0x1200000,input);
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.direct-image-upload.v1\",\"exact_rectangle_cases\":"<<cases<<",\"late_faults_unchanged\":"<<partial_faults
        <<",\"unreadable_padding_cases\":"<<padding_cases<<",\"unchanged_declines\":"<<declines<<",\"bgr24_overlap_cases\":"<<overlaps<<",\"timings\":[";
    for(unsigned mode=0;mode<4;++mode){const unsigned bytes=mode<2?3:4,src=mode==3?3:1,dst=mode==1 || mode==2?0:1;
        std::vector<std::uint8_t> output(Width*Height*4),reference(output.size());expected(reference,Width*4,dst,input,Width*bytes,src,bytes,Width,Height);
        std::array<std::array<std::uint64_t,2>,5> timing{};
        for(unsigned pair=0;pair<5;++pair)for(unsigned order=0;order<2;++order){const auto side=(pair+order)&1;
            const auto begin=std::chrono::steady_clock::now();
            for(unsigned n=0;n<Repetitions;++n){if(side)CHECK(g::transfer_source_pixels(memory,0x1200000,Width*bytes,bytes,PixelOrder(src),output,Width*4,PixelOrder(dst),Width,Height));
                else image_upload_before(memory,0x1200000,Width*bytes,bytes,PixelOrder(src),output,Width*4,PixelOrder(dst),Width,Height);}
            timing[pair][side]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();CHECK(output==reference);
        }
        if(mode)out<<',';out<<"{\"source_bytes\":"<<bytes<<",\"source_order\":"<<src<<",\"destination_order\":"<<dst<<",\"width\":"<<Width<<",\"height\":"<<Height<<",\"repetitions\":"<<Repetitions
            <<",\"full_image_staging_bytes_before\":"<<Width*Height*bytes<<",\"full_image_staging_bytes_after\":0,\"paired_ns\":[";
        for(unsigned n=0;n<timing.size();++n){if(n)out<<',';out<<'['<<timing[n][0]<<','<<timing[n][1]<<']';}out<<"]}";
    }
    out<<"],\"alpha_crop_and_padding_exact\":true,\"original_resources_opened\":false,\"game_launched\":false,\"screen_access\":false,\"device_opened\":false,\"whole_image_loading_measured\":false,\"passed\":true}\n";
    std::cout<<"PASS direct image upload: "<<cases<<" rectangles, "<<partial_faults<<" late faults, "<<padding_cases<<" padding guards.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
