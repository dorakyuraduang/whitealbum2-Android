// Complete cached sprite-function comparison through a memory-only device.
// No source EXE, game startup, window, GL context, PAK or frame loop.
#include "process.hpp"
#include "native_sprites.hpp"
#include "../engine/single_tile_geometry.hpp"
#include "../platform/render/submission_cache.hpp"
#include <winrecomp/win32_state.hpp>
#include <bit>
#include <chrono>
#include <cfenv>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <tuple>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
enum class Kind {unknown,d3d,device,texture,surface,vertexbuffer,indexbuffer,vertexshader,pixelshader,declaration,stateblock,swapchain,buffer,base_texture,resource};
#include "../port/d3d9_tables.inc"
namespace a=wa2::aot;
struct Recorder {
    using Words=std::map<std::pair<unsigned,unsigned>,unsigned>;
    struct Draw {Words words;unsigned texture,target,fvf;std::vector<std::uint8_t> bytes;};
    Words words;unsigned target{},fvf{},texture{};std::vector<Draw> draws;
    bool capture{true};std::uint64_t checksum{};unsigned draw_count{};
    void word(a::SpriteState group,unsigned key,unsigned value){words[{unsigned(group),key}]=value;}
    void append(std::span<const std::uint8_t> bytes){++draw_count;
        for(unsigned at=0;at<bytes.size();at+=8){std::uint64_t value;std::memcpy(&value,bytes.data()+at,8);checksum=(checksum^value)*0x9e3779b185ebca87ull;}
        if(capture)draws.push_back({words,texture,target,fvf,{bytes.begin(),bytes.end()}});}
    void api(const std::string& name,std::span<const unsigned> args,wr::Memory& m){
        if(!capture && name!="DrawPrimitiveUP")return;
        if(name=="SetRenderState")word(a::SpriteState::render,args[1],args[2]);
        if(name=="SetTextureStageState"){CHECK(!args[1]);word(a::SpriteState::stage,args[2],args[3]);}
        if(name=="SetSamplerState"){CHECK(!args[1]);word(a::SpriteState::sampler,args[2],args[3]);}
        if(name=="SetRenderTarget")target=args[2];if(name=="SetTexture"){CHECK(!args[1]);texture=args[2];}if(name=="SetFVF")fvf=args[1];
        if(name=="DrawPrimitiveUP"){CHECK(args[1]==5 && args[2]==2 && args[4]==32);std::array<std::uint8_t,128> bytes;m.copy_out(args[3],bytes);append(bytes);}
    }
    bool native(const a::SpriteCommand& command){target=command.target;if(capture)a::sprite_state_words(command,[&](auto group,unsigned key,unsigned value){word(group,key,value);});
        for(const auto& tile:command.tiles){texture=tile.texture;fvf=0x1c4;append({reinterpret_cast<const std::uint8_t*>(tile.vertices.data()),sizeof(tile.vertices)});texture=0;}return true;}
    void same(const Recorder& other)const{CHECK(words==other.words && target==other.target && fvf==other.fvf && texture==other.texture && draws.size()==other.draws.size());
        for(unsigned n=0;n<draws.size();++n){const auto& x=draws[n];const auto& y=other.draws[n];CHECK(x.words==y.words && x.target==y.target && x.texture==y.texture && x.fvf==y.fvf);
            if(x.bytes!=y.bytes){for(unsigned at=0;at<x.bytes.size();at+=4){unsigned u,v;std::memcpy(&u,x.bytes.data()+at,4);std::memcpy(&v,y.bytes.data()+at,4);if(u!=v)std::cerr<<"vertex word "<<at<<" source "<<std::hex<<u<<" native "<<v<<std::dec<<" real "<<std::bit_cast<float>(u)<<','<<std::bit_cast<float>(v)<<'\n';}CHECK(false);}}
    }
};
int main(int argc,char** argv){try{
    CHECK(argc==2);a::Runtime runtime;auto& p=runtime.process();Recorder recorded;
    std::vector<unsigned> methods;
    for(unsigned n=0;n<std::size(methods_device);++n){const auto method=methods_device[n];p.register_api("native-sprite-device",method.name,method.arguments,[&,name=std::string(method.name)](wr::Process& q){recorded.api(name,q.argument_snapshot(),q.memory);return 0u;});
        methods.push_back(p.resolve("native-sprite-device",method.name));if(wa2::graphics::direct_device_method(n))p.certify_leaf_api("native-sprite-device",method.name);}
    runtime.load();const auto data=p.allocate_bytes(65536),device=data,table=data+0x100,sprite=data+0x1000,image=data+0x2000;
    p.memory.store(device,table,32);for(unsigned n=0;n<methods.size();++n)p.memory.store(table+n*4,methods[n],32);
    for(const auto at:{0xb34438u,0xb3443cu,0xb34000u})p.memory.store(at,at==0xb34438?device:data+0x3000,32);
    p.memory.store(0xb34004,1,32);p.memory.store(0xbc830c,1,32);
    auto base=p.cpu;base.r[wr::ESP]-=0x8000;base.fp.put16(0,0x7f);
    a::Program program(p.memory);const auto returning=0xf0001234u;
    const auto f=[&](unsigned at,float value){p.memory.store(at,std::bit_cast<unsigned>(value),32);};
    const auto setup=[&](unsigned variant){
        std::array<std::uint8_t,256> empty{};p.memory.copy_in(sprite,empty);p.memory.copy_in(image,std::span(empty).first(128));
        p.memory.store(sprite+0x15,4,8);p.memory.store(sprite+0x3c,variant%12,32);p.memory.store(sprite+0x48,0xffff,16);p.memory.store(sprite+0x4c,0xffff,16);p.memory.store(sprite+0x40,variant%3,32);p.memory.store(sprite+0x44,(variant/12)%4,32);
        for(unsigned corner=0;corner<4;++corner){p.memory.store(sprite+0x53+corner*4,0xff000000u,32);p.memory.store(sprite+0x63+corner*4,variant&1?0x80ffffffu:0xff808080u,32);}
        const bool font=(variant/4)%2;const int width=font?3200:64,height=font?3840:64;
        p.memory.store(image,1,8);for(const auto [offset,value]:{std::pair{0x5c,width},{0x5e,height},{0x60,256},{0x62,256},{0x64,font?13:1},{0x66,font?15:1},{0x68,1}})p.memory.store(image+offset,value,16);
        p.memory.store(image+0x70,data+0x4000,32);for(unsigned n=0;n<256;++n)p.memory.store(data+0x4000+n*4,data+0x8000+n*16,32);
        const float x=-21.25f+variant*.25f,y=73.5f-variant*.5f;
        f(sprite+0x74,variant<96?0.f:float(((variant-96)*17)%360));
        f(sprite+0x78,x);f(sprite+0x7c,y);f(sprite+0x80,variant&2?.75f:1);f(sprite+0x84,variant&2?1.25f:1);
        for(unsigned n=0;n<4;++n){f(sprite+0x88+n*4,x+float(n&1?32:0));f(sprite+0x98+n*4,y+float(n>>1?32:0));f(sprite+0xac+n*4,1);}
        const float u=font?float(40+(variant*37)%3072):0,v=font?float(20+(variant*91)%3700):0;
        f(sprite+0xbc,u);f(sprite+0xc0,u+32);f(sprite+0xc4,v);f(sprite+0xc8,v+32);
        p.memory.store(0xb3442c,0,32);
    };
    const auto run=[&](bool native,bool capture=true){recorded=Recorder{};recorded.capture=capture;p.cpu=base;p.cpu.eip=0x43b7a0;p.memory.store(base.r[wr::ESP],returning,32);p.memory.store(base.r[wr::ESP]+4,sprite,32);p.memory.store(base.r[wr::ESP]+8,image,32);
        if(native){a::SpriteService service(p.memory,[](const a::SpriteCommand& c,void* owner){return static_cast<Recorder*>(owner)->native(c);},&recorded);
            const auto before=a::native_sprite_counts().accepted;CHECK(program.invoke_unmetered(p.cpu));CHECK(a::native_sprite_counts().accepted==before+1);}
        else{unsigned iterations{};while(p.cpu.eip!=returning){CHECK(++iterations<4096);a::LeafServiceScope leaf([](wr::Cpu&,wr::Memory&,void* owner){return static_cast<wr::Process*>(owner)->dispatch_leaf_api();},&p);if(!program.invoke_unmetered(p.cpu))CHECK(p.dispatch_api());}}
        CHECK(p.cpu.eip==returning && p.cpu.r[wr::ESP]==base.r[wr::ESP]+4 && p.cpu.fp.get16(0)==base.fp.get16(0));
        for(unsigned reg:{wr::EBX,wr::EBP,wr::ESI,wr::EDI})CHECK(p.cpu.r[reg]==base.r[reg]);return recorded;
    };
    unsigned cases{},tiles{};for(unsigned variant=0;variant<144;++variant){setup(variant);const auto original=run(false),native=run(true);
        try{original.same(native);}catch(...){std::cerr<<"sprite variant "<<variant<<'\n';throw;}++cases;tiles+=unsigned(native.draws.size());}
    // Pixel/alpha modes are source data, not host colour-space guesses.
    const auto palette_page=0x4c2000u;const auto permission=p.memory.permissions(palette_page);
    p.memory.protect(palette_page,4096,wr::Memory::Read|wr::Memory::Write);
    const auto palette_at=0x4c2290u+120*4;const auto old_palette=p.memory.load(palette_at,32);
    for(unsigned variant=0;variant<96;++variant){setup(variant);p.memory.store(sprite+0x52,120,8);
        p.memory.store(palette_at,((variant%3)<<24)|(((variant*37)&255)<<16)|(((variant*71)&255)<<8)|((variant*19)&255),32);
        for(unsigned n=0;n<4;++n){p.memory.store(sprite+0x53+n*4,0xff000000u|((variant*0x30571u)&0xffffffu),32);p.memory.store(sprite+0x63+n*4,(variant*0x1357973u),32);}
        run(false).same(run(true));++cases;
    }
    p.memory.store(palette_at,old_palette,32);p.memory.protect(palette_page,4096,permission);
    for(unsigned minimum:{0u,0x04000000u,0x08000000u,0x10000000u})for(unsigned magnification:{0u,0x400u,0x800u,0x1000u}){
        setup(7);p.memory.store(0xb3442c,1,32);p.memory.store(sprite+0x40,2,32);p.memory.store(0xb342c0,minimum|magnification,32);p.memory.store(0xb342a0,0x800,32);
        run(false).same(run(true));++cases;
    }
    p.memory.store(0xb342a0,0,32);p.memory.store(0xb342c0,0,32);
    // Compare the complete source function, rather than reimplementing the
    // optimized grid. Exercise exact tile edges, multi-tile rectangles, the
    // last partial column, full atlases, fractional fallback and half images.
    const std::array<std::array<float,4>,12> rectangles{{
        {0,32,0,32},{252,284,252,284},{256,288,256,288},{224,256,224,256},
        {3168,3200,3808,3840},{0,3200,0,3840},{-0.f,32,0,32},
        {.25f,32.75f,256.25f,288.75f},{255.5f,287.5f,255.5f,287.5f},
        {std::nextafter(256.f,0.f),288,256,288},{256,288,std::nextafter(256.f,0.f),288},
        {511,1025,767,1281}}};
    unsigned visible_grid_cases{};
    for(unsigned half=0;half<2;++half)for(unsigned flip:{0u,3u})for(const auto& rectangle:rectangles){
        setup(4);p.memory.store(sprite+0x44,flip,32);p.memory.store(image+4,half,8);
        f(sprite+0xbc,rectangle[0]);f(sprite+0xc0,rectangle[1]);f(sprite+0xc4,rectangle[2]);f(sprite+0xc8,rectangle[3]);
        run(false).same(run(true));++visible_grid_cases;
    }
    unsigned single_tile_cases{},single_tile_hits{},single_tile_fallbacks{};
    const auto compare_tile=[&]{const auto before=wa2::geometry::single_tile_diagnostics.accepted;
        run(false).same(run(true));++single_tile_cases;
        if(wa2::geometry::single_tile_diagnostics.accepted>before)++single_tile_hits;else ++single_tile_fallbacks;
    };
    for(unsigned size:{64u,128u,256u,512u})for(unsigned variant=0;variant<16;++variant){
        setup(variant%8);const unsigned tw=size,th=size/2;
        for(const auto [offset,value]:{std::pair{0x5cu,tw*4},{0x5eu,th*4},{0x60u,tw},{0x62u,th},{0x64u,4u},{0x66u,4u}})p.memory.store(image+offset,value,16);
        const float u=float((variant%4)*tw)+.25f,v=float((variant/4)*th)+.5f;
        f(sprite+0xbc,u);f(sprite+0xc0,u+32.5f);f(sprite+0xc4,v);f(sprite+0xc8,v+24.25f);compare_tile();
    }
    // Multi-cell crops, non-power-of-two cells and high-magnitude positions
    // stay on the original arithmetic, with the same complete source output.
    for(unsigned kind=0;kind<3;++kind)for(unsigned variant=0;variant<8;++variant){
        setup(variant);const unsigned tw=kind==1?192:256,th=kind==1?96:128;
        for(const auto [offset,value]:{std::pair{0x5cu,tw*4},{0x5eu,th*4},{0x60u,tw},{0x62u,th},{0x64u,4u},{0x66u,4u}})p.memory.store(image+offset,value,16);
        f(sprite+0xbc,kind==0?float(tw)-16:4);f(sprite+0xc0,kind==0?float(tw)+16:36);
        f(sprite+0xc4,4);f(sprite+0xc8,36);
        if(kind==2){f(sprite+0x78,0);f(sprite+0x7c,0);f(sprite+0x80,1);f(sprite+0x84,1);
            for(unsigned n=0;n<4;++n){f(sprite+0x88+n*4,32769.f+float(n&1?32:0));f(sprite+0x98+n*4,float(n>>1?32:0));}}
        compare_tile();
    }
    unsigned declines{};
    for(unsigned reason=0;reason<24;++reason){setup(1);p.cpu=base;p.cpu.eip=0x43b7a0;
        p.memory.store(base.r[wr::ESP],returning,32);p.memory.store(base.r[wr::ESP]+4,sprite,32);p.memory.store(base.r[wr::ESP]+8,image,32);
        switch(reason){case 0:p.cpu.fp.put16(0,0x37f);break;case 1:p.cpu.fp.bytes[4]=1;break;case 2:p.memory.store(sprite+0x48,2,16);break;
        case 3:p.memory.store(sprite+0x4c,2,16);break;case 4:p.memory.store(sprite+0x16,1,8);break;case 5:p.memory.store(sprite+0xdc,1,32);break;
        case 6:p.memory.store(sprite+0x63+4,17,32);break;case 7:f(sprite+0xbc,-1);break;case 8:f(sprite+0xc0,1000);break;
        case 9:p.memory.store(image+0x64,17,16);break;case 10:p.memory.store(0xb34004,0,32);break;case 11:p.memory.store(sprite+0x48,2,16);p.memory.store(0xa93fb8+2*128+0x68,2,16);break;}
        if(reason>=12){constexpr std::array<unsigned,12> exceptional{0x7f800000,0xff800000,0x7fc00000,0xffc00000,0x7f800001,0xff800001,0x00000001,0x80000001,0x007fffff,0x807fffff,0x00400000,0x80400000};
            p.memory.store(sprite+0x88,exceptional[reason-12],32);}
        const auto cpu=p.cpu;recorded=Recorder{};a::SpriteService service(p.memory,[](const a::SpriteCommand& c,void* owner){return static_cast<Recorder*>(owner)->native(c);},&recorded);
        CHECK(!a::native_draw_sprite(p.cpu,p.memory));CHECK(recorded.draws.empty() && p.cpu.r==cpu.r && p.cpu.fp.bytes==cpu.fp.bytes && p.cpu.eip==cpu.eip && p.cpu.flags==cpu.flags && p.cpu.defined_flags==cpu.defined_flags);++declines;
        p.memory.store(0xb34004,1,32);
    }
    // A renderer declining a supported packet must not commit the function.
    unsigned rejected_sinks{},environments{};
    setup(1);p.cpu=base;p.cpu.eip=0x43b7a0;p.memory.store(base.r[wr::ESP],returning,32);
    p.memory.store(base.r[wr::ESP]+4,sprite,32);p.memory.store(base.r[wr::ESP]+8,image,32);
    {const auto cpu=p.cpu;std::array<std::uint8_t,256> before,after;p.memory.copy_out(sprite,before);
        a::SpriteService service(p.memory,[](const a::SpriteCommand&,void*){return false;},nullptr);
        CHECK(!a::native_draw_sprite(p.cpu,p.memory));p.memory.copy_out(sprite,after);
        CHECK(p.cpu.r==cpu.r && p.cpu.fp.bytes==cpu.fp.bytes && p.cpu.eip==cpu.eip && p.cpu.flags==cpu.flags && before==after);++rejected_sinks;
    }
    // Host rounding/exception/errno state belongs to the host, not the source.
    for(int rounding:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){std::fenv_t saved;CHECK(!std::fegetenv(&saved));const auto error=errno;
        CHECK(!std::fesetround(rounding));std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);errno=ERANGE;
        const auto raised=std::fetestexcept(FE_ALL_EXCEPT);run(true,false);
        CHECK(std::fegetround()==rounding && std::fetestexcept(FE_ALL_EXCEPT)==raised && errno==ERANGE);
        CHECK(!std::fesetenv(&saved));errno=error;++environments;
    }
    std::array<std::array<std::array<std::uint64_t,2>,5>,3> timing{};
    const auto sample=[&](unsigned kind,bool native){setup(kind==2?4:1);p.memory.store(sprite+0x44,0,32);
        const unsigned count=kind==2?40:400;std::uint64_t checksum{};unsigned draws{};
        const auto begin=std::chrono::steady_clock::now();
        for(unsigned n=0;n<count;++n){const float x=float(n%26)*37.25f,y=float(n/26)*24.75f;
            f(sprite+0x78,x);f(sprite+0x7c,y);for(unsigned corner=0;corner<4;++corner){f(sprite+0x88+corner*4,x+(corner&1?32.f:0.f));f(sprite+0x98+corner*4,y+(corner>>1?32.f:0.f));}
            f(sprite+0x74,kind==1?float(n%16)*12.5f:0.f);
            if(kind==2){const unsigned index=4000+n*17;const float u=float(index%80)*40+4,v=float(index/80)*40+4;f(sprite+0xbc,u);f(sprite+0xc0,u+32);f(sprite+0xc4,v);f(sprite+0xc8,v+32);}
            const auto result=run(native,false);checksum=checksum*0x100000001b3ull+result.checksum;draws+=result.draw_count;
        }
        return std::array<std::uint64_t,3>{std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count()),checksum,draws};
    };
    for(unsigned kind=0;kind<3;++kind)for(unsigned pair=0;pair<5;++pair){std::array<std::array<std::uint64_t,3>,2> result;
        for(unsigned order=0;order<2;++order){const auto side=(pair+order)&1;result[side]=sample(kind,side);timing[kind][pair][side]=result[side][0];}
        CHECK(result[0][1]==result[1][1] && result[0][2]==result[1][2]);}
    std::ofstream out(argv[1]);out<<"{\"cases\":"<<cases<<",\"visible_grid_cases\":"<<visible_grid_cases<<",\"decline_cases\":"<<declines<<",\"renderer_decline_cases\":"<<rejected_sinks<<",\"host_environment_cases\":"<<environments<<",\"tiles\":"<<tiles
        <<",\"single_tile_source_cases\":"<<single_tile_cases<<",\"single_tile_candidate_hits\":"<<single_tile_hits<<",\"single_tile_fallback_cases\":"<<single_tile_fallbacks
        <<",\"vertex_bits_and_render_state_equal\":true,\"void_cdecl_abi_preserved\":true,\"raw_callee_volatile_x87_contract\":false,\"timings\":[";
    for(unsigned kind=0;kind<3;++kind){if(kind)out<<',';out<<"{\"sprites_per_sample\":"<<(kind==2?40:400)<<",\"rotated\":"<<(kind==1?"true":"false")<<",\"font_tiles\":"<<(kind==2?"true":"false")<<",\"paired_ns\":[";
        for(unsigned pair=0;pair<5;++pair){if(pair)out<<',';out<<'['<<timing[kind][pair][0]<<','<<timing[kind][pair][1]<<']';}out<<"]}";}
    out<<"],\"whole_game_fps_measured\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS native complete sprite path: "<<cases<<" cases, "<<tiles<<" tiles.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
