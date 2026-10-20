#include "../engine/subtitles.hpp"
#include "../recompiled/subtitles.hpp"
#include "../recompiled/functions.hpp"
#include "../platform/render/subtitle_layout.hpp"
#include <winrecomp/memory.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace sub=wa2::subtitles;
unsigned checks{},time_checks{},layout_checks{},abi_checks{},static_returns{},rejected{};
unsigned projection_cases{},projected_cues{},projected_glyphs{},invalid_viewports{};
template<class F>void reject(F f){bool failed{};try{f();}catch(const std::exception&){failed=true;}CHECK(failed);++rejected;}
const sub::Cue* reference(const sub::Track& track,std::uint64_t elapsed){
    for(const auto& cue:track.cues)if(cue.begin_ms<=elapsed && elapsed<cue.end_ms)return &cue;
    return nullptr;
}
void times_and_layout(){
    const auto tracks=sub::main_story_tracks();const auto spec=sub::font_spec();
    CHECK(tracks.size()==46 && sub::characters().size()==891);
    unsigned effects{},voices{},cue_count{};std::uint32_t last_key{};
    for(const auto& track:tracks){
        CHECK(track.key>last_key && !track.cues.empty());last_key=track.key;
        CHECK(sub::find_track(track.key)==&track);
        const auto kind=track.key<65536?sub::Kind::effect:sub::Kind::voice;
        (track.key<65536?effects:voices)++;
        for(const auto& cue:track.cues){
            ++cue_count;CHECK(cue.begin_ms<cue.end_ms && !cue.glyphs.empty());
            for(const auto t:std::array<std::uint64_t,6>{cue.begin_ms?cue.begin_ms-1:0, cue.begin_ms,
                cue.end_ms-1, cue.end_ms, std::uint64_t(cue.end_ms)+1, std::uint64_t(track.cues.back().end_ms)+50000}){
                sub::Playback player;CHECK(player.play(track.key,kind,3,1000));
                CHECK(player.current(1000+t)==reference(track,t));++time_checks;
            }
            const auto geometry=sub::layout(cue,spec,sub::characters().size());
            CHECK(geometry.size()==cue.glyphs.size());
            for(const auto& q:geometry){
                CHECK(std::isfinite(q.left) && q.left>=0 && q.right<=1280 && q.top>=0 && q.bottom<720);
                CHECK(q.u0>=0 && q.u0<q.u1 && q.u1<=1 && q.v0>=0 && q.v0<q.v1 && q.v1<=1);++layout_checks;
            }
        }
    }
    CHECK(effects==6 && voices==40 && cue_count==315);
    CHECK(!sub::find_track(0) && !sub::find_track(0xffffffff));
    sub::Playback player;CHECK(player.play(8365,sub::Kind::effect,2,1000));
    CHECK(!player.play(tracks.back().key,sub::Kind::voice,0,1001));
    const auto* first=player.current(1000);CHECK(first);
    player.stop(sub::Kind::voice,2);player.stop(sub::Kind::effect,3);CHECK(player.current(1001)==first);
    CHECK(!player.play(0xffffffff,sub::Kind::voice,0,1002,true) && player.current(1002)==first);
    CHECK(player.play(tracks.back().key,sub::Kind::voice,0,1003,true));
    player.stop(sub::Kind::voice,0);CHECK(!player.current(1004));
    CHECK(!player.play(8365,sub::Kind::voice,0,1005));
    CHECK(player.play(8365,sub::Kind::effect,2,1006));player.reset();CHECK(!player.current(1007));
    std::vector<std::uint16_t> long_text(512,0);sub::Cue long_cue{0,1000,long_text};
    CHECK(sub::layout(long_cue,spec,sub::characters().size()).size()==512);
    long_text.push_back(0);long_cue.glyphs=long_text;reject([&]{sub::layout(long_cue,spec,891);});
    const std::array<std::uint16_t,1> unknown{891};reject([&]{sub::layout(sub::Cue{0,1,unknown},spec,891);});
    auto bad=spec;bad.advance=0;reject([&]{sub::layout(*first,bad,891);});
}
void presentation_layout(){
    namespace g=wa2::graphics;
    struct Case {
        std::array<int,2> drawable;
        std::array<unsigned,2> buffer;
        g::FrameRect source;
        std::optional<g::FrameRect> destination;
        float x,y,scale;
    };
    const std::array<Case,16> cases{{
        // Regression: the source renders/presents 1280x720 inside a larger
        // allocation. Multiplying by the allocation made it 1.5x too large
        // and moved the caption centre from x=640 to x=960.
        {{1280,720},{1920,1080},{0,0,1280,720},{},0,0,1},
        {{1280,720},{1280,720},{0,0,1280,720},{},0,0,1},
        {{1280,720},{2400,1080},{0,0,1280,720},{},0,0,1},
        {{1280,720},{1920,1080},{320,180,1600,900},{},0,0,1},
        {{1280,720},{1920,1080},{0,0,1920,1080},{},0,0,1},
        {{1280,720},{640,360},{0,0,640,360},{},0,0,1},
        {{2400,1080},{2400,1080},{0,0,1280,720},{},240,0,1.5f},
        {{2400,1080},{1920,1080},{0,0,1280,720},{},240,0,1.5f},
        {{2400,1080},{1280,720},{0,0,1280,720},{},240,0,1.5f},
        {{2340,1080},{1920,1080},{0,0,1920,1080},{},210,0,1.5f},
        {{1920,1200},{1920,1080},{0,0,1920,1080},{},0,60,1.5f},
        {{2560,1440},{1920,1080},{0,0,1280,720},{},0,0,2},
        {{720,1280},{1920,1080},{0,0,1280,720},{},0,437,.5625f},
        {{1024,768},{1920,1080},{0,0,1280,720},{},0,96,.8f},
        {{1280,720},{1920,1080},{0,0,1280,720},g::FrameRect{160,90,1120,630},160,90,.75f},
        {{2400,1080},{1920,1080},{0,0,1920,1080},g::FrameRect{320,180,960,540},720,270,.75f},
    }};
    class MemorySurface final:public g::SurfaceHost {
    public:
        SDL_Window* acquire(std::uint32_t,std::uint32_t,std::uint32_t,bool)override{throw std::runtime_error("subtitle test cannot open a window");}
        bool embedded_gl()const noexcept override{return false;}
        void source_cursor(std::int32_t,std::int32_t)override{}int cursor_visible(bool)override{return 0;}
        std::array<int,2> client_size(std::uint32_t)const override{return {1280,720};}
        void frame_submitted()override{throw std::runtime_error("subtitle test cannot submit a frame");}
    } host;
    const auto spec=sub::font_spec();
    const auto close=[](float a,float b){return std::abs(a-b)<.002f;};
    for(const auto& c:cases){
        const auto viewport=host.client_viewport(0,c.drawable[0],c.drawable[1]);
        const auto frame=g::layout_frame(c.buffer[0],c.buffer[1],c.source,c.destination,viewport);CHECK(frame);
        const auto projection=g::subtitle_projection(c.drawable[0],c.drawable[1],frame->destination);CHECK(projection);
        CHECK(close(projection->origin[0],c.x) && close(projection->origin[1],c.y) && close(projection->scale,c.scale));
        CHECK(projection->clip==frame->destination);++projection_cases;
        // Round-trip the exact shader input through the physical viewport.
        const auto pixel=[&](float x,float y){const auto p=projection->position(x,y);
            CHECK(std::isfinite(p[0]) && std::isfinite(p[1]) && p[2]==0 && p[3]==1);
            return std::array{(p[0]+1)*c.drawable[0]/2,(1-p[1])*c.drawable[1]/2};};
        const auto centre=pixel(640,40);
        CHECK(close(centre[0],(frame->destination[0]+frame->destination[2])/2.f));
        CHECK(close(centre[1],c.y+40*c.scale));
        for(const auto& track:sub::main_story_tracks())for(const auto& cue:track.cues){
            const auto quads=sub::layout(cue,spec,sub::characters().size());++projected_cues;
            for(const auto& q:quads){
                const auto a=pixel(q.left,q.top),b=pixel(q.right,q.bottom);
                CHECK(a[0]>=frame->destination[0]-.002f && b[0]<=frame->destination[2]+.002f &&
                      a[1]>=frame->destination[1]-.002f && b[1]<=frame->destination[3]+.002f);
                CHECK(close(b[0]-a[0],spec.cell*c.scale) && close(b[1]-a[1],spec.cell*c.scale));++projected_glyphs;
            }
            // Every wrapped line is independently centred, using the pen
            // advance rather than the transparent padding of the font tile.
            for(std::size_t first=0;first<quads.size();){
                auto end=first+1;while(end<quads.size() && quads[end].top==quads[first].top)++end;
                const auto a=pixel(quads[first].left,quads[first].top);
                const auto middle=a[0]+(spec.origin_x+(end-first)*spec.advance/2.f)*c.scale;
                CHECK(close(middle,centre[0]));first=end;
            }
        }
    }
    // Non-widescreen destinations still preserve the font aspect and fit.
    const auto square=g::subtitle_projection(1024,768,{0,0,1024,768});CHECK(square);
    CHECK(close(square->origin[0],0) && close(square->origin[1],96) && close(square->scale,.8f));
    for(const auto& bad:std::array<g::FrameRect,7>{{{0,0,0,720},{0,0,1280,0},{-1,0,1280,720},
        {0,-1,1280,720},{0,0,1281,720},{0,0,1280,721},{1,1,0x7fffffff,0x7fffffff}}}){
        CHECK(!g::subtitle_projection(1280,720,bad));++invalid_viewports;
    }
    CHECK(!g::subtitle_projection(0,720,{0,0,1280,720}));++invalid_viewports;
    CHECK(!g::subtitle_projection(1280,0,{0,0,1280,720}));++invalid_viewports;
}
void source_abi(bool linear){
    wr::Memory memory;if(linear)CHECK(memory.enable_linear_memory());
    constexpr std::uint32_t args=0x200000;
    for(auto at:{args,0x4be000u,0xa39000u})memory.map(at,4096,wr::Memory::Read|wr::Memory::Write);
    const auto store=[&](unsigned n,std::uint32_t value){memory.store(args+n*4,value,32);};
    wr::Cpu cpu;cpu.r[wr::EAX]=0xdeadbeef;cpu.r[wr::ESP]=args;cpu.flags=0x202;cpu.defined_flags=0x8d5;cpu.eip=0x12345678;
    const auto before=cpu;sub::Playback player;
    CHECK(!wa2::aot::find_source_subtitles(memory));
    auto bound=wa2::aot::bind_source_subtitles(memory);
    CHECK(wa2::aot::find_source_subtitles(memory)==bound);
    reject([&]{wa2::aot::bind_source_subtitles(memory);});
    wa2::aot::unbind_source_subtitles(memory,&player);CHECK(wa2::aot::find_source_subtitles(memory)==bound);
    wa2::aot::unbind_source_subtitles(memory,bound.get());CHECK(!wa2::aot::find_source_subtitles(memory));
    auto replacement=wa2::aot::bind_source_subtitles(memory);CHECK(replacement!=bound);
    wa2::aot::unbind_source_subtitles(memory,bound.get());CHECK(wa2::aot::find_source_subtitles(memory)==replacement);
    wa2::aot::unbind_source_subtitles(memory,replacement.get());CHECK(!wa2::aot::find_source_subtitles(memory));
    const auto notify=[&](std::uint32_t entry,std::uint64_t now){wa2::aot::source_subtitle_return(player,entry,cpu,memory,args,now);++abi_checks;};
    store(0,2);store(1,8365);notify(0x40f400,1000);const auto* se=player.current(1000);CHECK(se);
    store(0,1);notify(0x40f590,1001);CHECK(player.current(1001)==se);
    store(0,2);notify(0x40f590,1002);CHECK(!player.current(1002));
    const auto key=1008u<<16|166u;CHECK(sub::find_track(key));
    store(0,3);store(2,1008);store(3,66);memory.store(0xa391ec,100,32);
    notify(0x40eda0,2000);CHECK(player.current(2000)==&sub::find_track(key)->cues[0]);
    store(0,2);notify(0x40ec30,2001);CHECK(player.current(2001));
    store(0,3);notify(0x40ec30,2002);CHECK(!player.current(2002));
    store(2,0xffffffff);memory.store(0x4be4cc,1008,32);memory.store(0xa391e0,3001,32);
    notify(0x40eda0,3000);CHECK(player.current(3000)==&sub::find_track(key)->cues[0]);
    player.reset();memory.store(0x4be4cc,0xffffffff,32);memory.store(0xa391e0,1008,32);
    notify(0x40eda0,4000);CHECK(player.current(4000)==&sub::find_track(key)->cues[0]);
    const auto tracks=sub::main_story_tracks();const auto interview=std::find_if(tracks.begin(),tracks.end(),[](const auto& t){return t.key>>16==9500;});
    CHECK(interview!=tracks.end());store(0,(interview->key&65535)+1);notify(0x406270,5000);
    CHECK(player.current(5000+interview->cues[0].begin_ms)==&interview->cues[0]);
    store(0,0);notify(0x40ec30,5001);CHECK(!player.current(5001));
    player.reset();store(0,2);store(1,8365);
    struct Sink {sub::Playback* player;std::uint64_t now;};Sink sink{&player,6000};
    const auto callback=+[](std::uint32_t entry,const wr::Cpu& c,wr::Memory& m,std::uint32_t a,void* owner){
        auto& s=*static_cast<Sink*>(owner);wa2::aot::source_subtitle_return(*s.player,entry,c,m,a,s.now);
    };
    {
        wa2::aot::SubtitleEventScope scope{memory,callback,&sink};
        wa2::aot::notify_subtitle_return<true>(0x40f400,cpu,memory,args);CHECK(!player.current(6000));
        wr::Memory other;wa2::aot::notify_subtitle_return<false>(0x40f400,cpu,other,args);CHECK(!player.current(6000));
        wa2::aot::notify_subtitle_return<false>(0x40f400,cpu,memory,args);CHECK(player.current(6000)==se);
        sub::Playback nested;Sink nested_sink{&nested,6000};
        {wa2::aot::SubtitleEventScope nested_scope{memory,callback,&nested_sink};
            wa2::aot::notify_subtitle_return<false>(0x40f400,cpu,memory,args);CHECK(nested.current(6000)==se);}
        wa2::aot::notify_subtitle_return<false>(0x40f590,cpu,memory,args);CHECK(!player.current(6001));CHECK(nested.current(6001)==se);
    }
    wa2::aot::notify_subtitle_return<false>(0x40f400,cpu,memory,args);CHECK(!player.current(6002));
    // Execute the actual generated empty-channel stop functions in owned RAM.
    // This covers the injected return path and cdecl argument address without
    // a Process, game entry, device, original executable or external resource.
    memory.map(args-4096,4096,wr::Memory::Read|wr::Memory::Write);
    memory.map(0x401000,0xa0000,wr::Memory::Read|wr::Memory::Write);
    for(const auto& function:wa2::aot::functions())for(const auto& span:function.code)memory.copy_in(span.address,span.bytes);
    memory.protect(0x401000,0xa0000,wr::Memory::Read|wr::Memory::Execute);
    memory.map(0x5e7000,4096,wr::Memory::Read|wr::Memory::Write);
    // The cached source uses -1 for an unused device handle; zero is a valid
    // handle and would request the audio manager, absent in this RAM fixture.
    memory.store(0x5e7368+52*2,0xffffffff,32);
    memory.store(0xa390c8+56*2,0xffffffff,32);
    wa2::aot::Program program{memory};
    for(const auto event:std::array<std::pair<std::uint32_t,sub::Kind>,2>{{{0x40f590,sub::Kind::effect},{0x40ec30,sub::Kind::voice}}}){
        player.reset();const auto audio=event.second==sub::Kind::effect?8365u:key;
        CHECK(player.play(audio,event.second,2,8000));store(0,2);store(1,0);
        auto state=cpu;state.eip=event.first;state.r[wr::ESP]=args-4;
        memory.store(args-4,0xf0001234,32);sink.now=8001;
        {wa2::aot::SubtitleEventScope scope{memory,callback,&sink};CHECK(program.invoke_unmetered(state));}
        CHECK(state.eip==0xf0001234 && state.r[wr::ESP]==args);
        CHECK(!player.current(8001));++static_returns;
    }
    store(1,8365);
    reject([&]{notify(0x404040,7000);});
    CHECK(cpu.r==before.r && cpu.eip==before.eip && cpu.flags==before.flags && cpu.defined_flags==before.defined_flags &&
        cpu.fp.bytes==before.fp.bytes && cpu.selectors==before.selectors && cpu.fs_base==before.fs_base &&
        cpu.gs_base==before.gs_base && cpu.fs_valid==before.fs_valid && cpu.gs_valid==before.gs_valid);
    CHECK(memory.load(args,32)==2 && memory.load(args+4,32)==8365);
    CHECK(memory.load(0xa391ec,32)==100);
}
int main(int argc,char** argv){try{
    CHECK(argc==2);times_and_layout();presentation_layout();source_abi(false);source_abi(true);
    const auto spec=sub::font_spec();const auto font=sub::decode_font(sub::font_coverage(),spec,891);
    CHECK(font.width==spec.cell*spec.columns && font.height==spec.cell*spec.rows);
    std::uint64_t hash=14695981039346656037ull,visible{};
    for(auto b:font.rgba){hash^=b;hash*=1099511628211ull;}
    for(std::size_t i=3;i<font.rgba.size();i+=4)visible+=font.rgba[i]!=0;
    CHECK(visible>891*20);
    reject([&]{sub::decode_font({},spec,891);});
    auto packed=std::vector<std::uint8_t>(sub::font_coverage().begin(),sub::font_coverage().end());
    packed.push_back(0);reject([&]{sub::decode_font(packed,spec,891);});
    packed.pop_back();packed.pop_back();reject([&]{sub::decode_font(packed,spec,891);});
    auto bad=spec;bad.cell=41;reject([&]{sub::decode_font(sub::font_coverage(),bad,891);});
    std::ofstream output(argv[1]);output<<"{\"schema\":\"wa2.main-story-subtitles.v1\",\"checks\":"<<checks
        <<",\"time_boundaries\":"<<time_checks<<",\"glyph_quads\":"<<layout_checks<<",\"source_audio_events\":"<<abi_checks
        <<",\"generated_static_returns\":"<<static_returns
        <<",\"presentation_cases\":"<<projection_cases<<",\"projected_cues\":"<<projected_cues<<",\"projected_glyphs\":"<<projected_glyphs
        <<",\"invalid_viewports\":"<<invalid_viewports<<",\"backbuffer_independent_projection\":true"
        <<",\"rejections\":"<<rejected<<",\"tracks\":46,\"cues\":315,\"characters\":891,\"font_rgba_bytes\":"<<font.rgba.size()
        <<",\"font_rgba_fnv64\":\""<<hash<<"\",\"special_contents_included\":false,\"source_executable_opened\":false,\"game_launched\":false,\"graphics_device_opened\":false,\"screen_access\":false,\"passed\":true}\n";
    CHECK(output);std::cout<<"PASS main-story subtitles: "<<checks<<" checks, 315 cues, both source-memory layouts, embedded coverage and geometry.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
