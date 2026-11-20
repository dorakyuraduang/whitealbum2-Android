// Actual Special source calls vs the main game's shared drawing implementation.
// Constructed RAM and recorded CPU interfaces only; no startup/frame/device.
#include "../recompiled/special/runtime.hpp"
#include "../recompiled/special/render.hpp"
#include "../recompiled/glyph_command_replay_control.hpp"
#include "../recompiled/glyph_budget.hpp"
#include "render_profile.hpp"
#include "../platform/render/submission_cache.hpp"
#include <winrecomp/win32_state.hpp>
#include <bit>
#include <cfenv>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
enum class Kind {unknown,d3d,device,texture,surface,vertexbuffer,indexbuffer,vertexshader,pixelshader,declaration,stateblock,swapchain,buffer,base_texture,resource};
#include "../port/d3d9_tables.inc"
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check at ")+std::to_string(__LINE__)+": " #x);}while(false)
namespace s=wa2::special;namespace a=wa2::aot;using wr::U32;
struct Draw {
    std::map<std::pair<unsigned,U32>,U32> state;U32 texture{},target{},fvf{};
    std::vector<std::uint8_t> vertices;
    bool operator==(const Draw&)const=default;
};
struct Recorder {
    std::map<std::pair<unsigned,U32>,U32> state;
    std::vector<Draw> draws;U32 texture{},target{},fvf{},surface{};unsigned gets{},releases{};
    void word(a::SpriteState group,U32 key,U32 value){state[{unsigned(group),key}]=value;}
    void api(const std::string& name,std::span<const U32> args,wr::Memory& memory){
        if(name=="SetRenderState")word(a::SpriteState::render,args[1],args[2]);
        if(name=="SetTextureStageState"){CHECK(!args[1]);word(a::SpriteState::stage,args[2],args[3]);}
        if(name=="SetSamplerState"){CHECK(!args[1]);word(a::SpriteState::sampler,args[2],args[3]);}
        if(name=="SetRenderTarget")target=args[2];
        if(name=="SetTexture"){CHECK(!args[1]);texture=args[2];}
        if(name=="SetFVF")fvf=args[1];
        if(name=="GetSurfaceLevel"){CHECK(args[1]==0);memory.store(args[2],surface,32);++gets;}
        if(name=="Release")++releases;
        if(name=="DrawPrimitiveUP"){
            CHECK(args[1]==5 && args[2]==2 && args[4]==32);
            Draw draw{state,texture,target,fvf,std::vector<std::uint8_t>(128)};
            memory.copy_out(args[3],draw.vertices);draws.push_back(std::move(draw));
        }
    }
    bool native(const a::SpriteCommand& command,wr::Memory& memory){
        target=command.target;
        if(command.target_kind==a::SpriteTarget::texture_level_zero){
            CHECK(command.surface_result==s::drawing_source.globals+8);
            memory.store(command.surface_result,surface,32);target=surface;++gets;++releases;
        }
        a::sprite_state_words(command,[&](auto group,U32 key,U32 value){word(group,key,value);});
        for(const auto& tile:command.tiles){texture=tile.texture;fvf=0x1c4;
            const auto* bytes=reinterpret_cast<const std::uint8_t*>(tile.vertices.data());
            draws.push_back({state,texture,target,fvf,{bytes,bytes+sizeof(tile.vertices)}});texture=0;
        }
        return true;
    }
    void same(const Recorder& other)const{
        CHECK(state==other.state && texture==other.texture && target==other.target && fvf==other.fvf);
        CHECK(gets==other.gets && releases==other.releases);
        if(draws!=other.draws){
            std::cerr<<"draw count "<<draws.size()<<'/'<<other.draws.size()<<'\n';
            if(draws.size()==other.draws.size())for(unsigned i=0;i<draws.size();++i)for(unsigned n=0;n<128;n+=4){U32 x,y;
                std::memcpy(&x,draws[i].vertices.data()+n,4);std::memcpy(&y,other.draws[i].vertices.data()+n,4);
                if(x!=y){std::cerr<<"vertex "<<i<<" word "<<n<<' '<<std::hex<<x<<'/'<<y<<std::dec<<'\n';break;}}
            CHECK(false);
        }
    }
};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned sprite_cases{},glyph_cases{},declines{},proof_guards{},yield_cases{},tiles{};std::uint64_t replays{};
    for(bool linear:{false,true}){
        wr::ProcessOptions options;options.prefer_linear_memory=linear;
        s::Runtime runtime(options);auto& p=runtime.process();Recorder recorder;
        const auto bind=[&](const char* library,std::span<const Method> methods,bool device){
            std::vector<U32> table;
            for(unsigned slot=0;slot<methods.size();++slot){const auto method=methods[slot];
                p.register_api(library,method.name,method.arguments,[&,name=std::string(method.name)](wr::Process& q){recorder.api(name,q.argument_snapshot(),q.memory);return 0u;});
                table.push_back(p.resolve(library,method.name));
                if(device && wa2::graphics::direct_device_method(slot))p.certify_leaf_api(library,method.name);
            }return table;
        };
        const auto devices=bind("special-device",methods_device,true),textures=bind("special-texture",methods_texture,false),surfaces=bind("special-surface",methods_surface,false);
        const auto image=s::initial_image();p.load(image);
        const auto data=p.allocate_bytes(65536),device=data,device_table=data+0x100,sprite=data+0x1000,resource=data+0x2000,surface=data+0x7000;
        const auto texture_table=data+0x800,surface_table=data+0xc00;
        const auto put_table=[&](U32 at,const auto& words){for(unsigned n=0;n<words.size();++n)p.memory.store(at+n*4,words[n],32);};
        put_table(device_table,devices);put_table(texture_table,textures);put_table(surface_table,surfaces);
        p.memory.store(device,device_table,32);p.memory.store(surface,surface_table,32);
        for(unsigned n=0;n<256;++n)p.memory.store(data+0x8000+n*16,texture_table,32);
        const auto source=s::drawing_source;
        for(const auto [at,value]:{std::pair{source.globals+4,1u},{source.globals+0x438,device},{source.globals, surface},{source.globals+0x43c,surface},{source.sse2,1u},{source.trig_dispatch,1u}})p.memory.store(at,value,32);
        p.memory.store(source.font_styles,32,8);
        auto base=p.cpu;base.r[wr::ESP]-=0x8000;base.fp.put16(0,0x7f);
        for(unsigned n:{wr::EBX,wr::ESI,wr::EDI,wr::EBP})base.r[n]=0x12345000+n;
        a::StaticProgram program(p.memory,s::functions(),s::locate);
        const auto f=[&](U32 at,float value){p.memory.store(at,std::bit_cast<U32>(value),32);};
        const auto setup=[&](unsigned variant,bool glyph){
            std::array<std::uint8_t,256> empty{};p.memory.copy_in(sprite,empty);p.memory.copy_in(resource,std::span(empty).first(128));
            p.memory.store(sprite+0x15,4,8);p.memory.store(sprite+0x3c,variant%12,32);
            p.memory.store(sprite+0x48,variant>=120 && !glyph?0:0xffff,16);p.memory.store(sprite+0x4a,0,16);p.memory.store(sprite+0x4c,0xffff,16);
            p.memory.store(sprite+0x40,variant%3,32);p.memory.store(sprite+0x44,glyph?0:(variant/12)%4,32);
            for(unsigned n=0;n<4;++n){p.memory.store(sprite+0x53+n*4,0xff304050u+variant,32);p.memory.store(sprite+0x63+n*4,0xffa0b0c0u-variant,32);}
            const float x=-21.25f+variant*.25f,y=73.5f-variant*.5f;
            f(sprite+0x74,!glyph && variant>=96 && variant<120?float((variant*17)%360):0.f);
            f(sprite+0x78,x);f(sprite+0x7c,y);f(sprite+0x80,glyph?1.f:variant&2?.75f:1.f);f(sprite+0x84,glyph?1.f:variant&2?1.25f:1.f);
            for(unsigned n=0;n<4;++n){f(sprite+0x88+n*4,x+float(n&1?32:0));f(sprite+0x98+n*4,y+float(n>>1?32:0));f(sprite+0xac+n*4,1);}
            f(sprite+0xa8,0);const bool atlas=glyph || variant&4;
            const float u=atlas?float((variant*37)%3000):0,v=atlas?float((variant*91)%3700):0;
            f(sprite+0xbc,u);f(sprite+0xc0,u+32);f(sprite+0xc4,v);f(sprite+0xc8,v+32);
            for(const auto [offset,value]:{std::pair{0x5c,atlas?3200:64},{0x5e,atlas?3840:64},{0x60,256},{0x62,256},{0x64,atlas?13:1},{0x66,atlas?15:1},{0x68,1}})p.memory.store(resource+offset,value,16);
            p.memory.store(resource,1,8);p.memory.store(resource+0x70,data+0x4000,32);
            for(unsigned n=0;n<256;++n)p.memory.store(data+0x4000+n*4,data+0x8000+n*16,32);
            std::array<std::uint8_t,128> row;p.memory.copy_out(resource,row);
            for(unsigned n=0;n<3;++n)p.memory.copy_in(source.images+n*128,row);
            p.memory.store(source.globals+0x428,0,32);p.memory.store(source.globals+0x42c,variant&1,32);
            p.memory.store(source.globals+0x2a0,variant&2?0x800:0,32);p.memory.store(source.globals+0x2c0,variant&1?0x10001000:0,32);
        };
        struct Result {Recorder recorder;wr::Cpu cpu;std::vector<std::uint8_t> bytes,images;U32 scratch;};
        const auto run=[&](unsigned variant,bool glyph,bool native,unsigned precision=0){
            recorder=Recorder{};recorder.surface=surface;p.cpu=base;p.cpu.fp.put16(0,std::uint16_t(0x7f|(precision<<8)));
            constexpr U32 returned=0xf0001234;const auto sp=base.r[wr::ESP];
            p.memory.store(source.globals+8,0xa5a5a5a5,32);
            p.memory.store(sp,returned,32);p.cpu.eip=glyph?source.glyph_entry:source.sprite_entry;
            if(glyph){
                std::array<std::uint8_t,256> bytes;p.memory.copy_out(sprite,bytes);p.memory.copy_in(sp+4,bytes);
                const std::array<U32,9> args{variant&1?0x4081u:0x41u,0,0,32,32,variant%5,32,0xff102030u+variant,variant%3};
                for(unsigned n=0;n<args.size();++n)p.memory.store(sp+0x104+n*4,args[n],32);
            }else{p.memory.store(sp+4,sprite,32);p.memory.store(sp+8,resource,32);}
            std::optional<s::DrawingReferenceScope> reference;if(!native)reference.emplace();
            std::optional<a::SpriteService> service;
            std::pair owner{&recorder,&p.memory};
            if(native)service.emplace(p.memory,[](const a::SpriteCommand& c,void* owner){auto& pair=*static_cast<std::pair<Recorder*,wr::Memory*>*>(owner);return pair.first->native(c,*pair.second);},&owner);
            const auto environment=std::fegetround(),exceptions=std::fetestexcept(FE_ALL_EXCEPT),error=errno;
            for(unsigned n=0;p.cpu.eip!=returned;++n){CHECK(n<8192);
                a::LeafServiceScope leaf([](wr::Cpu&,wr::Memory&,void* owner){return static_cast<wr::Process*>(owner)->dispatch_leaf_api();},&p);
                if(!program.invoke(p.cpu))CHECK(p.dispatch_api());
            }
            CHECK(p.cpu.r[wr::ESP]==sp+4 && p.cpu.fp.get16(0)==(0x7f|(precision<<8)) && p.cpu.fp.bytes[4]==0);
            for(unsigned r:{wr::EBX,wr::ESI,wr::EDI,wr::EBP})CHECK(p.cpu.r[r]==base.r[r]);
            if(native)CHECK(std::fegetround()==environment && std::fetestexcept(FE_ALL_EXCEPT)==exceptions && errno==error);
            Result result{recorder,p.cpu,std::vector<std::uint8_t>(65536),std::vector<std::uint8_t>(384),p.memory.load(source.globals+8,32)};
            p.memory.copy_out(data,result.bytes);p.memory.copy_out(source.images,result.images);return result;
        };
        const auto verify=[&](unsigned variant,bool glyph,unsigned precision=0){
            setup(variant,glyph);const auto original=run(variant,glyph,false,precision);
            const auto before=glyph?a::native_glyph_counts().accepted:a::native_sprite_counts().accepted;
            const auto native=run(variant,glyph,true,precision);
            try{original.recorder.same(native.recorder);CHECK(original.bytes==native.bytes && original.images==native.images && original.scratch==native.scratch);}
            catch(...){std::cerr<<"linear "<<linear<<" variant "<<variant<<" glyph "<<glyph<<" precision "<<precision<<'\n';throw;}
            if((glyph?a::native_glyph_counts().accepted:a::native_sprite_counts().accepted)<=before){
                std::cerr<<"native admission: linear "<<linear<<" variant "<<variant<<" glyph "<<glyph<<" precision "<<precision
                    <<' '<<s::sprite_report()<<' '<<s::glyph_report()<<'\n';CHECK(false);
            }
            CHECK(!native.recorder.draws.empty());tiles+=unsigned(native.recorder.draws.size());
            if(glyph)++glyph_cases;else ++sprite_cases;
        };
        for(unsigned n=0;n<144;++n)verify(n,false);
        for(unsigned precision:{0u,2u,3u})for(unsigned n=0;n<36;++n)verify(n,true,precision);
        // Actual native glyph calls must reuse checked commands, while live
        // palette, image/handle and style changes invalidate the cached value.
        setup(7,true);const auto hits=a::glyph_command_replay_hits;
        for(unsigned n=0;n<5;++n){const auto before=run(7,true,false),after=run(7,true,true);before.recorder.same(after.recorder);}
        CHECK(a::glyph_command_replay_hits>hits);replays+=a::glyph_command_replay_hits-hits;
        CHECK(!recorder.draws.empty());
        const auto live_texture=recorder.draws.front().texture;
        CHECK(live_texture>=data+0x8000 && (live_texture-data-0x8000)%16==0);
        const auto live_slot=(live_texture-data-0x8000)/16;CHECK(live_slot<256);
        for(unsigned kind=0;kind<4;++kind){
            if(kind==0)p.memory.store(source.font_styles,31,8);
            if(kind==1)p.memory.store(data+0x4000+live_slot*4,data+0x8000+((live_slot+1)%256)*16,32);
            if(kind==2)p.memory.store(source.images+128+0x68,2,16);
            if(kind==3)p.memory.store(source.globals+0x42c,0,32);
            const auto previous_hits=a::glyph_command_replay_hits;
            run(7,true,false).recorder.same(run(7,true,true).recorder);
            if(a::glyph_command_replay_hits!=previous_hits){std::cerr<<"cache dependency "<<kind<<" linear "<<linear<<'\n';CHECK(false);}++glyph_cases;
        }
        // Reuse the main profile's independent glyph allowance and exact
        // defer/resume policy instead of falling back after 32 leaf calls.
        setup(7,true);run(7,true,true);
        auto incoming=base;incoming.eip=source.glyph_entry;
        const auto accepted_before=a::native_glyph_counts().accepted,deferrals_before=a::glyph_budget_deferrals;
        std::pair owner{&recorder,&p.memory};
        a::SpriteService receiver(p.memory,[](const a::SpriteCommand& c,void* owner){auto& pair=*static_cast<std::pair<Recorder*,wr::Memory*>*>(owner);return pair.first->native(c,*pair.second);},&owner);
        {
            a::GlyphCallScope allowance(p.memory,3);
            for(unsigned n=0;n<4;++n){p.cpu=incoming;CHECK(s::draw_glyph(p.cpu,p.memory));
                if(n<3)CHECK(p.cpu.eip==0xf0001234 && p.cpu.r[wr::ESP]==incoming.r[wr::ESP]+4);
                else CHECK(p.cpu.eip==incoming.eip && p.cpu.r==incoming.r && p.cpu.fp.bytes==incoming.fp.bytes && p.cpu.flags==incoming.flags);
            }
            CHECK(*a::GlyphCallScope::remaining(p.memory)==0);++yield_cases;
        }
        CHECK(a::glyph_budget_deferrals==deferrals_before+1 && a::native_glyph_counts().accepted==accepted_before+3);
        {a::GlyphCallScope allowance(p.memory,3);CHECK(s::draw_glyph(p.cpu,p.memory));CHECK(p.cpu.eip==0xf0001234);++yield_cases;}
        CHECK(a::native_glyph_counts().accepted==accepted_before+4);
        // Unsupported admission is side-effect-free before the static body.
        setup(2,false);
        for(unsigned kind=0;kind<6;++kind){
            p.cpu=base;p.cpu.eip=source.sprite_entry;p.memory.store(base.r[wr::ESP],0xf0001234,32);
            p.memory.store(base.r[wr::ESP]+4,sprite,32);p.memory.store(base.r[wr::ESP]+8,resource,32);
            if(kind==0)p.cpu.fp.put16(0,0x27f);if(kind==1)p.cpu.fp.put16(0,0x47f);
            if(kind==2)p.memory.store(sprite+0x4c,0,16);
            if(kind==3)p.memory.store(base.r[wr::ESP]+4,0x21000000,32);
            if(kind==4)p.memory.store(sprite+0x53,0xffabcdef,32);
            if(kind==5)p.memory.store(resource+0x64,17,16);
            const auto cpu=p.cpu;std::array<std::uint8_t,256> before,after;p.memory.copy_out(sprite,before);
            std::pair owner{&recorder,&p.memory};a::SpriteService service(p.memory,[](const a::SpriteCommand&,void*){throw std::runtime_error("declined input reached receiver");return false;},&owner);
            CHECK(!s::draw_sprite(p.cpu,p.memory));p.memory.copy_out(sprite,after);
            CHECK(before==after && cpu.r==p.cpu.r && cpu.fp.bytes==p.cpu.fp.bytes && cpu.flags==p.cpu.flags && cpu.eip==p.cpu.eip);++declines;
            setup(2,false);
        }
        // The port is never entered for writable or changed source code.
        const auto page=source.sprite_entry&~4095u,byte=p.memory.load(source.sprite_entry,8);
        for(unsigned kind=0;kind<2;++kind){p.memory.protect(page,4096,wr::Memory::Read|wr::Memory::Write|wr::Memory::Execute);
            if(kind)p.memory.store(source.sprite_entry,byte^1,8);
            if(kind)p.memory.protect(page,4096,wr::Memory::Read|wr::Memory::Execute);
            p.cpu=base;p.cpu.eip=source.sprite_entry;bool rejected{};
            try{program.invoke(p.cpu);}catch(const wr::GuestFault& fault){rejected=fault.kind==wr::FaultKind::unsupported;}
            CHECK(rejected);++proof_guards;
            p.memory.protect(page,4096,wr::Memory::Read|wr::Memory::Write|wr::Memory::Execute);p.memory.store(source.sprite_entry,byte,8);p.memory.protect(page,4096,wr::Memory::Read|wr::Memory::Execute);
        }
        CHECK(p.executed_instructions==0);
    }
    CHECK(sprite_cases==288 && glyph_cases==224 && declines==12 && proof_guards==4 && yield_cases==4 && replays>0);
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.special-shared-drawing.v1\",\"sprite_source_cases\":"<<sprite_cases<<",\"glyph_source_cases\":"<<glyph_cases
        <<",\"decline_cases\":"<<declines<<",\"immutable_code_guards\":"<<proof_guards<<",\"glyph_budget_yield_cases\":"<<yield_cases<<",\"tiles_compared\":"<<tiles<<",\"glyph_command_replay_hits\":"<<replays
        <<",\"raw_vertices_render_state_and_game_objects_equal\":true,\"cdecl_nonvolatile_fp_control_and_host_environment_preserved\":true,\"raw_callee_volatile_state_contract\":false,\"same_main_drawing_implementation\":true,"
          "\"paged_and_linear_memory\":true,\"game_launched\":false,\"screen_access\":false,\"window_or_device_created\":false,\"passed\":true}\n";
    CHECK(out);std::cout<<"PASS Special shared drawing: "<<sprite_cases<<" sprites, "<<glyph_cases<<" glyphs; no game or device.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
