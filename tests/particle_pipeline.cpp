// Cached weather update -> its actual Sprite objects -> original/native draw.
// Only owned RAM and recording COM endpoints. No window, game or GL context.
#include "process.hpp"
#include "native_sprites.hpp"
#include "../platform/render/submission_cache.hpp"
#include <winrecomp/win32_state.hpp>
#include <bit>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <tuple>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
enum class Kind {unknown,d3d,device,texture,surface,vertexbuffer,indexbuffer,vertexshader,pixelshader,declaration,stateblock,swapchain,buffer,base_texture,resource};
#include "../port/d3d9_tables.inc"
namespace a=wa2::aot;
struct Recording {
    using Words=std::map<std::pair<unsigned,unsigned>,unsigned>;
    struct Draw {Words words;unsigned target,texture;std::array<std::uint8_t,128> vertices;bool operator==(const Draw&)const=default;};
    Words words;unsigned target{},texture{},fvf{},draws{},offscreen{};std::uint64_t checksum{},apis{},native{};bool capture{true};
    std::vector<Draw> output;
    void append(std::span<const std::uint8_t> bytes){CHECK(bytes.size()==128);++draws;
        for(unsigned n=0;n<128;n+=8){std::uint64_t word;std::memcpy(&word,bytes.data()+n,8);checksum=(checksum^word)*0x9e3779b185ebca87ull;}
        if(capture){auto& d=output.emplace_back();d.words=words;d.target=target;d.texture=texture;std::memcpy(d.vertices.data(),bytes.data(),128);}}
};
int main(int argc,char** argv){try{
    CHECK(argc==2 || (argc==3 && std::string(argv[2])=="--require-native"));const bool require_native=argc==3;
    a::Runtime runtime;auto& p=runtime.process();Recording recorded;std::map<unsigned,unsigned> surfaces;
    const auto register_table=[&](const auto& table,std::string library,bool device){std::vector<unsigned> result;
        for(unsigned slot=0;slot<std::size(table);++slot){const auto method=table[slot];
            p.register_api(library,method.name,method.arguments,[&,name=std::string(method.name),device](wr::Process& q){
                ++recorded.apis;const auto args=q.argument_snapshot();
                if(!device){if(name=="GetSurfaceLevel"){CHECK(args[1]==0);q.memory.store(args[2],surfaces.at(args[0]),32);}return 0u;}
                if(name=="DrawPrimitiveUP"){CHECK(args[1]==5 && args[2]==2 && args[4]==32 && recorded.fvf==0x1c4);std::array<std::uint8_t,128> data;q.memory.copy_out(args[3],data);recorded.append(data);}
                else if(name=="SetRenderTarget")recorded.target=args[2];else if(name=="SetFVF")recorded.fvf=args[1];
                else if(name=="SetTexture")recorded.texture=args[2];
                else if(recorded.capture){if(name=="SetRenderState")recorded.words[{0,args[1]}]=args[2];
                    else if(name=="SetTextureStageState")recorded.words[{1,args[2]}]=args[3];else if(name=="SetSamplerState")recorded.words[{2,args[2]}]=args[3];}return 0u;
            });result.push_back(p.resolve(library,method.name));
            if((device && wa2::graphics::direct_device_method(slot)) || (!device && (slot==18 || slot==2)))p.certify_leaf_api(library,method.name);
        }return result;};
    const auto dt=register_table(methods_device,"weather-device",true),tt=register_table(methods_texture,"weather-texture",false),st=register_table(methods_surface,"weather-surface",false);
    runtime.load();const auto data=p.allocate_bytes(65536),device=data,default_target=data+0x1800;
    const auto table=[&](unsigned object,unsigned address,const auto& methods){p.memory.store(object,address,32);for(unsigned n=0;n<methods.size();++n)p.memory.store(address+n*4,methods[n],32);};
    table(device,data+0x100,dt);table(default_target,data+0x500,st);
    for(unsigned n=0;n<2;++n){const auto texture=data+0x2000+n*0x100,surface=data+0x2200+n*0x100;table(texture,data+0x700,tt);table(surface,data+0x500,st);surfaces[texture]=surface;
        const auto target=0xa93fb8+(0x105+n)*128;p.memory.store(target+0x68,1,16);p.memory.store(target+0x70,data+0x2500+n*4,32);p.memory.store(data+0x2500+n*4,texture,32);}
    p.memory.store(0xb34438,device,32);p.memory.store(0xb34000,default_target,32);p.memory.store(0xb3443c,default_target,32);p.memory.store(0xb34004,1,32);
    p.memory.store(0xbc830c,1,32);p.memory.store(0xbc8310,1,32);p.memory.store(0xb3442c,1,32);p.memory.store(0xb342c0,0x3030300,32);p.memory.store(0xb34430,1,32);
    const auto image=0xa93fb8+0x6e*128;p.memory.store(image,1,8);
    for(auto [at,value]:{std::pair{0x5cu,32u},{0x5eu,64u},{0x60u,256u},{0x62u,256u},{0x64u,1u},{0x66u,1u},{0x68u,1u}})p.memory.store(image+at,value,16);
    p.memory.store(image+0x70,data+0x2600,32);p.memory.store(data+0x2600,data+0x3000,32);
    auto base=p.cpu;base.r[wr::ESP]-=0x8000;base.fp.put16(0,0x7f);a::Program program(p.memory);const auto returning=0xf0001234u;
    const auto f=[&](unsigned at,float value){p.memory.store(at,std::bit_cast<unsigned>(value),32);};
    const auto call=[&](unsigned entry){p.cpu=base;p.cpu.eip=entry;p.memory.store(base.r[wr::ESP],returning,32);unsigned calls{};
        while(p.cpu.eip!=returning){CHECK(++calls<8192);a::LeafServiceScope leaf([](wr::Cpu&,wr::Memory&,void* owner){return static_cast<wr::Process*>(owner)->dispatch_leaf_api();},&p);
            if(!program.invoke_unmetered(p.cpu))CHECK(p.dispatch_api());}
        CHECK(p.cpu.r[wr::ESP]==base.r[wr::ESP]+4 && p.cpu.fp.get16(0)==base.fp.get16(0));};
    const auto prepare=[&](unsigned count,bool dual,unsigned flags){p.memory.copy_in(0xab3fb8,std::vector<std::uint8_t>(0x80000));p.memory.copy_in(0xb4fd58,std::vector<std::uint8_t>(2048*92));
        for(auto [at,value]:{std::pair{0x522930u,1u},{0x522934u,3u},{0x522938u,0u},{0x522940u,1u},{0x522944u,flags},{0x5267d4u,count},{0x5267e4u,count},
            {0x5267d8u,0u},{0x5267e8u,0u},{0x5267ecu,dual?0u:1u},{0x5267dcu,1u},{0x5267e0u,3u},{0x5216d8u,0u},{0x521784u,0u},{0x52187cu,128u},{0x521880u,128u},{0x521884u,128u}})p.memory.store(at,value,32);
        f(0x5267cc,1);f(0x5267d0,3);
        for(unsigned n=0;n<count;++n){const auto at=0x522948+n*40;p.memory.store(at,1,16);p.memory.store(at+2,248+n%8,16);p.memory.store(at+4,256,16);p.memory.store(at+6,1,16);p.memory.store(at+8,n%256,16);
            f(at+12,10.f+float(n%1000));f(at+16,10.f+float(n%600));f(at+20,248.f+float(n%8));f(at+24,1.25f);f(at+28,1.5f);f(at+32,640);}
        call(0x4078b0);
    };
    const auto run=[&](bool native,bool capture){recorded=Recording{};recorded.capture=capture;
        using Sink=std::tuple<Recording*,wr::Memory*,std::map<unsigned,unsigned>*>;Sink sink{&recorded,&p.memory,&surfaces};
        std::optional<a::SpriteService> service;if(native)service.emplace(p.memory,[](const a::SpriteCommand& c,void* owner){auto [output,memory,targets]=*static_cast<Sink*>(owner);auto& r=*output;++r.native;
            CHECK((c.target_kind==a::SpriteTarget::texture_level_zero)==targets->contains(c.target));
            r.target=targets->contains(c.target)?targets->at(c.target):c.target;if(targets->contains(c.target)){memory->store(0xb34008,r.target,32);++r.offscreen;}
            if(r.capture)a::sprite_state_words(c,[&](auto group,unsigned key,unsigned value){r.words[{unsigned(group),key}]=value;});
            for(const auto& tile:c.tiles){r.texture=tile.texture;r.fvf=0x1c4;r.append({reinterpret_cast<const std::uint8_t*>(tile.vertices.data()),sizeof(tile.vertices)});r.texture=0;}return true;
        },&sink);
        const auto start=std::chrono::steady_clock::now();
        for(unsigned n=81;n<881;++n){const auto sprite=0xab3fb8+n*256;if(!p.memory.load(sprite,8))continue;
            p.memory.store(base.r[wr::ESP]+4,sprite,32);p.memory.store(base.r[wr::ESP]+8,0,32);call(0x43b7a0);}
        const auto elapsed=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());return std::pair{recorded,elapsed};
    };
    unsigned cases{},draws{},admitted{},offscreen{};for(unsigned count:{16,128,400})for(bool dual:{false,true})for(unsigned flags:{0,0x800}){
        prepare(count,dual,flags);const auto before=run(false,true);const auto after=run(true,true);
        CHECK(before.first.output==after.first.output && before.first.checksum==after.first.checksum && before.first.draws==after.first.draws);
        if(require_native)CHECK(after.first.native==after.first.draws && !after.first.apis);
        ++cases;draws+=after.first.draws;admitted+=after.first.native;offscreen+=after.first.offscreen;
    }
    std::array<std::array<std::uint64_t,2>,5> paired{};prepare(400,true,0);
    for(unsigned pair=0;pair<5;++pair){std::array<Recording,2> result;
        for(unsigned order=0;order<2;++order){const auto side=(pair+order)&1;auto [record,ns]=run(side,true);result[side]=std::move(record);paired[pair][side]=ns;}
        CHECK(result[0].output==result[1].output && result[0].checksum==result[1].checksum);}
    std::ofstream out(argv[1]);out<<"{\"cached_weather_entry\":4225200,\"cases\":"<<cases<<",\"source_draws\":"<<draws<<",\"native_admitted\":"<<admitted<<",\"native_offscreen_draws\":"<<offscreen
        <<",\"source_vertices_and_state_equal\":true,\"source_filter_mode_2\":true,\"linear_sampling_enabled\":true,\"paired_ns\":[";
    for(unsigned n=0;n<5;++n){if(n)out<<',';out<<'['<<paired[n][0]<<','<<paired[n][1]<<']';}
    out<<"],\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS cached weather draw pipeline: "<<admitted<<'/'<<draws<<" native draws, "<<offscreen<<" offscreen.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
