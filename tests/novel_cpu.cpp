// Finite text-helper calls with constructed text, font metadata and a recording
// device. No game startup, original resource, window, GL context or screen.
#include "../recompiled/process.hpp"
#include "../recompiled/native_sprites.hpp"
#include "../recompiled/native_glyphs.hpp"
#include "../recompiled/glyph_budget.hpp"
#include "../recompiled/glyph_resources.hpp"
#include "../recompiled/text_advance.hpp"
#include "../recompiled/text_jump_budget.hpp"
#include "../recompiled/host_calls.hpp"
#include "../engine/glyph_index.hpp"
#include "../platform/render/submission_cache.hpp"
#include <winrecomp/win32_state.hpp>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("novel check at ")+std::to_string(__LINE__)+": "+#x);}while(false)
using wr::U32;
namespace a=wa2::aot;
enum class Kind {unknown,d3d,device,texture,surface,vertexbuffer,indexbuffer,vertexshader,pixelshader,declaration,stateblock,swapchain,buffer,base_texture,resource};
#include "../port/d3d9_tables.inc"

struct Tile {
    U32 texture;
    std::array<U32,32> vertices;
    bool operator==(const Tile&)const=default;
};
struct Recording {
    std::map<std::string,unsigned> calls;
    std::vector<Tile> tiles;
    U32 texture{};
    static bool submit(const a::SpriteCommand& command,void* owner){
        auto& self=*static_cast<Recording*>(owner);
        for(const auto& input:command.tiles){auto& tile=self.tiles.emplace_back();tile.texture=input.texture;
            std::memcpy(tile.vertices.data(),input.vertices.data(),sizeof(tile.vertices));}
        return true;
    }
    void reset(){calls.clear();tiles.clear();texture=0;}
};
#include "../tests/novel_layout_oracle.hpp"

static std::uint64_t cpu_time_ns(){
#ifdef __ANDROID__
    timespec time{};
    CHECK(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&time)==0);
    return std::uint64_t(time.tv_sec)*1000000000u+time.tv_nsec;
#else
    return static_cast<std::uint64_t>(double(std::clock())*1000000000.0/CLOCKS_PER_SEC);
#endif
}

int main(int argc,char** argv){try{
    CHECK(argc==2);a::Runtime runtime;auto& p=runtime.process();Recording recorded;
    p.replace_api("user32.dll","MessageBoxA",4,[](wr::Process&)->U32{throw std::runtime_error("Constructed text exceeds source text-helper contract");});
    std::vector<U32> vtable;
    for(unsigned slot=0;slot<std::size(methods_device);++slot){const auto& method=methods_device[slot];
        p.register_api("novel-recording-device",method.name,method.arguments,[&,name=std::string(method.name)](wr::Process& q){
            ++recorded.calls[name];const auto args=q.argument_snapshot();
            if(name=="SetTexture" && !args[1])recorded.texture=args[2];
            if(name=="DrawPrimitiveUP"){
                CHECK(args[1]==5 && args[2]==2 && args[4]==sizeof(a::SpriteVertex));
                auto& tile=recorded.tiles.emplace_back();tile.texture=recorded.texture;
                q.memory.copy_out(args[3],{reinterpret_cast<std::uint8_t*>(tile.vertices.data()),sizeof(tile.vertices)});
            }return 0u;
        });vtable.push_back(p.resolve("novel-recording-device",method.name));
        if(wa2::graphics::direct_device_method(slot))p.certify_leaf_api("novel-recording-device",method.name);
    }
    runtime.load();const auto data=p.allocate_bytes(65536),device=data,table=data+0x100,output=data+0x1000;
    p.memory.store(device,table,32);for(unsigned n=0;n<vtable.size();++n)p.memory.store(table+n*4,vtable[n],32);
    p.memory.store(0xb34438,device,32);p.memory.store(0xb34004,1,32);
    p.memory.store(0xb34000,data+0x2000,32);p.memory.store(0xb3443c,data+0x2000,32);
    p.memory.store(0xbc830c,1,32);p.memory.store(0xbc8310,1,32);
    p.memory.store(0xa823b1,28,8);p.memory.store(0xa823b2,32,8);p.memory.store(0xa823b3,40,8);
    p.memory.store(output+0x48,0xffff,16);p.memory.store(output+0x4a,0,16);p.memory.store(output+0x4c,0xffff,16);
    p.memory.store(output+0x15,4,8);p.memory.store(output+0x80,0x3f800000,32);p.memory.store(output+0x84,0x3f800000,32);
    for(unsigned font=0;font<4;++font){const auto resource=0xa93fb8+font*128;
        p.memory.store(resource,1,8);p.memory.store(resource+0x5c,font<2?3200:1920,16);p.memory.store(resource+0x5e,font<2?3840:2304,16);
        p.memory.store(resource+0x60,256,16);p.memory.store(resource+0x62,256,16);p.memory.store(resource+0x64,font<2?13:8,16);
        p.memory.store(resource+0x66,font<2?15:9,16);p.memory.store(resource+0x68,1,16);p.memory.store(resource+0x70,data+0x4000,32);
    }
    for(unsigned n=0;n<256;++n)p.memory.store(data+0x4000+n*4,data+0x8000+n*16,32);
    p.callback(p.resolve("kernel32.dll","GetProcessHeap"),{});
    // Numeric formatting consults CRT per-thread locale state. Construct its
    // two documented source slots directly; do not run CRT/game startup.
    const auto tls=p.callback(p.resolve("kernel32.dll","TlsAlloc"),{});
    const auto fls=p.callback(p.resolve("kernel32.dll","FlsAlloc"),std::array<U32,1>{0});
    p.state().tls[tls]=p.resolve("kernel32.dll","FlsGetValue");
    p.callback(p.resolve("kernel32.dll","FlsSetValue"),std::array<U32,2>{fls,data+0xc000});
    p.memory.store(0x4d1cc0,fls,32);p.memory.store(0x4d1cc4,tls,32);
    p.memory.store(data+0xc06c,p.memory.load(0x4d1cb0,32),32);
    p.memory.store(data+0xc068,p.memory.load(0x4d1ad0,32),32);
    const auto initial_cpu=p.cpu;
    a::compact_text_advance_enabled.store(false,std::memory_order_relaxed);
    std::array<std::uint8_t,0x2000> initial_output;p.memory.copy_out(output,initial_output);
    std::array<std::uint8_t,256> sprite;p.memory.copy_out(output,sprite);
    std::ofstream report(argv[1]);
    // A second executable can link the exact same fixture object against a
    // retained AOT archive. Compare explicit CPU/output words, not padding or
    // scratch stack bytes which are outside the source return ABI.
    std::ofstream snapshot(std::string(argv[1])+".bin",std::ios::binary);
    const auto word=[&](U32 v){snapshot.write(reinterpret_cast<const char*>(&v),4);};
    report<<"{\"pages\":[";bool comma{};unsigned exact_pages{},unchanged_page_checks{},formatted_pages{};
    const std::array<std::pair<std::string,unsigned>,6> formats{{
        {"ABC\nDEF",6},{"A^B~C`D",7},{"A\\nBC",3},{"A\\<B\\>C",5},
        {"<C1:ABC>",3},{"<S2:ABCDE>",5}
    }};
    for(unsigned variant=0;variant<4+formats.size();++variant){
        const bool two_byte=variant>=2;const unsigned count=variant&1?(two_byte?80:200):40;
        const auto expected_count=variant<4?count:formats[variant-4].second;
        std::string text;for(unsigned n=0;n<count;++n){
            if(two_byte)text+=n&1?"\x90\xcb":"\x8d\xef";else text+=char('A'+n%26);
        }if(variant>=4)text=formats[variant-4].first;
        const auto raw=p.put_string(text);
        std::array<U32,26> args{output,0,14,28,4,0,14,0,0,0,28,raw,0xffffffffu,0,255,255,255,255,0,0,0,0,0,0,0,1};
        args[24]=data+0x3000;
        const auto run=[&](bool native){
            recorded.reset();p.cpu=initial_cpu;p.cpu.fp.put16(0,0x7f);p.memory.copy_in(output,initial_output);
            const auto start=std::chrono::steady_clock::now();
            {std::optional<a::SpriteService> sink;if(native)sink.emplace(p.memory,Recording::submit,&recorded);p.callback(0x446500,args,false);}
            return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        };
        const auto static_ns=run(false);const auto expected=recorded.tiles;CHECK(expected.size()>=expected_count);
        const auto expected_cpu=p.cpu;std::array<std::uint8_t,0x2000> expected_output;p.memory.copy_out(output,expected_output);
        const auto before=a::native_glyph_counts();const auto resources_before=a::glyph_resource_counts;const auto flow_before=a::text_jump_counts;
        const auto native_ns=run(true);const auto after=a::native_glyph_counts();const auto resources_after=a::glyph_resource_counts;
        const auto single_reads=resources_after.single_texture_handle_reads-resources_before.single_texture_handle_reads;
        const auto copied_words=resources_after.full_texture_handle_words-resources_before.full_texture_handle_words;
        const auto flow_after=a::text_jump_counts;
        CHECK(single_reads>0 && single_reads+copied_words<expected_count*195);
        const auto check_result=[&]{
            CHECK(recorded.tiles==expected && recorded.calls.empty());
            CHECK(p.cpu.eip==expected_cpu.eip && p.cpu.r[wr::ESP]==expected_cpu.r[wr::ESP]);
            CHECK(p.cpu.fp.get16(0)==expected_cpu.fp.get16(0) && !p.cpu.fp.bytes[4]);
            for(auto reg:{wr::EBX,wr::EBP,wr::ESI,wr::EDI})CHECK(p.cpu.r[reg]==expected_cpu.r[reg]);
            std::array<std::uint8_t,0x2000> actual;p.memory.copy_out(output,actual);CHECK(actual==expected_output);
        };
        check_result();
        if(variant>=4)std::cout<<"Format "<<variant<<": glyphs "<<after.accepted-before.accepted<<", declines "<<after.declined-before.declined<<", tiles "<<expected.size()<<'\n';
        CHECK(after.accepted-before.accepted==expected_count && after.declined==before.declined);++exact_pages;if(variant>=4)++formatted_pages;
        word(variant);word(U32(recorded.tiles.size()));
        for(const auto& tile:recorded.tiles){word(tile.texture);for(auto value:tile.vertices)word(value);}
        // EAX/result and callee-preserved registers are part of the public ABI.
        for(auto reg:{wr::EAX,wr::EBX,wr::EBP,wr::ESI,wr::EDI,wr::ESP})word(p.cpu.r[reg]);
        word(p.cpu.eip);snapshot.write(reinterpret_cast<const char*>(p.cpu.fp.bytes.data()),p.cpu.fp.bytes.size());
        snapshot.write(reinterpret_cast<const char*>(expected_output.data()),expected_output.size());
        // Resubmit the already-visible page without input/resource changes.
        for(unsigned repeat=0;repeat<2;++repeat){run(true);check_result();++unchanged_page_checks;}
        if(comma)report<<',';comma=true;
        report<<"{\"characters\":"<<expected_count<<",\"two_byte\":"<<(variant<4&&two_byte?"true":"false")<<",\"formatted\":"<<(variant>=4?"true":"false")<<",\"native_glyphs\":"<<after.accepted-before.accepted
            <<",\"native_tiles\":"<<expected.size()<<",\"legacy_draws\":0,\"legacy_state_calls\":0,\"single_handle_reads\":"<<single_reads<<",\"copied_handle_words\":"<<copied_words
            <<",\"previous_minimum_handle_words\":"<<expected_count*195<<",\"local_jumps\":"<<flow_after.local_jumps-flow_before.local_jumps<<",\"local_yields\":"<<flow_after.budget_yields-flow_before.budget_yields<<",\"static_chain_ns\":"<<static_ns<<",\"native_ns\":"<<native_ns<<'}';
        std::cout<<"Constructed page: "<<expected_count<<" chars, exact "<<expected.size()<<" tiles, no legacy calls, static/native ns "<<static_ns<<'/'<<native_ns<<'\n';
    }
    // The same fixed CJK page, entered fully visible or revealed in stages.
    // Preserve guest state between reveals, including the page output and
    // glyph caches. The recording sink observes the real shared text routine;
    // this fixture does not invoke save loading, script progression or GL.
    constexpr U32 page_length=96;
    std::string reveal_text="<C1:";
    for(U32 i=0;i<page_length;++i)reveal_text+=i%2?"\x90\xcb":"\x8d\xef";
    reveal_text+='>';
    const U32 reveal_string=p.put_string(reveal_text);
    std::array<U32,26> reveal_args{output,0,14,28,4,0,14,0,0,0,28,reveal_string,
        0,0,255,255,255,255,0,0,0,0,0,0,data+0x3000,1};
    const U32 cursor=reveal_args[24];
    std::array<std::uint8_t,16> original_cursor;
    p.memory.copy_out(cursor,original_cursor);
    const auto begin_page=[&]{
        p.cpu=initial_cpu;
        p.cpu.fp.put16(0,0x007fu);
        p.memory.copy_in(output,initial_output);
        p.memory.copy_in(cursor,original_cursor);
        p.memory.store(0xb34544,0,32);
    };
    struct RevealWork {
        std::uint64_t glyphs,tiles,single_handle_reads,jumps;
        bool operator==(const RevealWork&)const=default;
    };
    U32 text_result{};
    const auto frame=[&](U32 visible,U32 style=0,bool append=false){
        reveal_args[12]=visible;
        reveal_args[18]=style;
        if(!append)recorded.reset();
        const auto previous_tiles=recorded.tiles.size();
        const auto before=a::native_glyph_counts();
        const auto handle_reads=a::glyph_resource_counts.single_texture_handle_reads;
        const auto jumps=a::text_jump_counts.local_jumps;
        a::SpriteService sink(p.memory,Recording::submit,&recorded);
        text_result=p.callback(0x446500,reveal_args,false);
        const auto after=a::native_glyph_counts();
        CHECK(after.declined==before.declined && recorded.calls.empty());
        return RevealWork{after.accepted-before.accepted,recorded.tiles.size()-previous_tiles,
            a::glyph_resource_counts.single_texture_handle_reads-handle_reads,a::text_jump_counts.local_jumps-jumps};
    };
    constexpr U32 immediately_visible=0xffffffffu;
    begin_page();
    const auto direct=frame(immediately_visible);
    const auto opaque_tiles=recorded.tiles;
    CHECK(direct.glyphs==page_length && direct.tiles==page_length);
    begin_page();
    const auto direct_finite=frame(page_length);
    const auto finite_tiles=recorded.tiles;
    CHECK(direct_finite.glyphs==direct.glyphs && direct_finite.tiles==direct.tiles);
    unsigned partially_faded{};
    for(std::size_t i=0;i<finite_tiles.size();++i){
        auto faded=finite_tiles[i];
        const auto& opaque=opaque_tiles[i];
        if(faded!=opaque)++partially_faded;
        for(unsigned vertex=0;vertex<4;++vertex){
            const unsigned colour=vertex*8+4;
            faded.vertices[colour]=(faded.vertices[colour]&0x00ffffffu)|(opaque.vertices[colour]&0xff000000u);
        }
        CHECK(faded==opaque);
    }
    CHECK(partially_faded==15);
    begin_page();
    constexpr std::array<U32,4> limits{8,24,56,page_length};
    std::array<RevealWork,limits.size()> stages{};
    for(std::size_t i=0;i<limits.size();++i){
        stages[i]=frame(limits[i]);
        CHECK(stages[i].glyphs==limits[i]);
    }
    CHECK(stages.back()==direct_finite && recorded.tiles==finite_tiles);
    CHECK(frame(page_length)==direct_finite && recorded.tiles==finite_tiles);
    const auto saturated=frame(page_length+16);
    CHECK(saturated.glyphs==direct.glyphs && recorded.tiles==opaque_tiles);
    CHECK(frame(immediately_visible)==direct && recorded.tiles==opaque_tiles);
    // Mirror the adjacent styled/plain text calls at 0x442ec6 and 0x442f40,
    // including their shared cursor outputs and intervening result store.
    reveal_args[7]=cursor+8;
    reveal_args[8]=cursor+12;
    const auto both_passes=[&](U32 visible){
        const auto styled=frame(visible,1);
        p.memory.store(cursor+4,text_result,32);
        const auto plain=frame(visible,0,true);
        p.memory.store(cursor+4,text_result,32);
        return std::array<RevealWork,2>{styled,plain};
    };
    begin_page();
    const auto direct_pair=both_passes(immediately_visible);
    const auto direct_pair_tiles=recorded.tiles;
    CHECK(direct_pair[0].glyphs==page_length && direct_pair[1].glyphs==page_length);
    begin_page();
    const auto finite_pair=both_passes(page_length);
    const auto finite_pair_tiles=recorded.tiles;
    begin_page();
    std::array<std::array<RevealWork,2>,limits.size()> paired_stages{};
    for(std::size_t i=0;i<limits.size();++i){
        paired_stages[i]=both_passes(limits[i]);
        CHECK(paired_stages[i][0].glyphs==limits[i] && paired_stages[i][1].glyphs==limits[i]);
    }
    CHECK(paired_stages.back()==finite_pair && recorded.tiles==finite_pair_tiles);
    CHECK(both_passes(immediately_visible)==direct_pair && recorded.tiles==direct_pair_tiles);
    begin_page();
    recorded.reset();
    for(U32 style:{1u,0u}){
        reveal_args[12]=immediately_visible;
        reveal_args[18]=style;
        p.memory.store(cursor+4,p.callback(0x446500,reveal_args,false),32);
    }
    CHECK(!recorded.calls.empty() && recorded.tiles==direct_pair_tiles);
    // Exercise the same two source calls after independent reveal/cursor state resets.
    // The finite path changes alpha on its last 15 glyphs on both passes.
    const char* timing_mode=std::getenv("WA2_NOVEL_TIMING");
    const std::string_view mode=timing_mode?timing_mode:"";
    unsigned layout_cases{};
    if(mode=="layout"){
        begin_page();
        layout_cases=novel_layout_test::verify(p,initial_cpu,output,reveal_args);
    }
    const auto advance_hits_before=a::compact_text_advance_hits;
    for(auto visible:{immediately_visible,page_length}){
        // A retained pre-optimization library has no candidate admissions.
        if(mode=="historical")break;
        a::compact_text_advance_enabled.store(false,std::memory_order_relaxed);
        begin_page();
        const auto reference_work=both_passes(visible);
        const auto reference_cpu=p.cpu;
        const auto reference_return=text_result;
        std::array<std::uint8_t,16> reference_cursor;p.memory.copy_out(cursor,reference_cursor);
        std::array<std::uint8_t,0x2000> reference_output;p.memory.copy_out(output,reference_output);
        a::compact_text_advance_enabled.store(true,std::memory_order_relaxed);
        begin_page();
        const auto previous_hits=a::compact_text_advance_hits;
        CHECK(both_passes(visible)==reference_work && text_result==reference_return);
        CHECK(a::compact_text_advance_hits>previous_hits);
        CHECK(recorded.tiles==(visible==immediately_visible?direct_pair_tiles:finite_pair_tiles));
        CHECK(p.cpu.r==reference_cpu.r && p.cpu.eip==reference_cpu.eip && p.cpu.fp.bytes==reference_cpu.fp.bytes &&
              p.cpu.flags==reference_cpu.flags && p.cpu.defined_flags==reference_cpu.defined_flags);
        std::array<std::uint8_t,16> actual_cursor;p.memory.copy_out(cursor,actual_cursor);
        std::array<std::uint8_t,0x2000> actual_output;p.memory.copy_out(output,actual_output);
        CHECK(actual_cursor==reference_cursor && actual_output==reference_output);
    }
    const auto advance_hits=a::compact_text_advance_hits-advance_hits_before;
    a::compact_text_advance_enabled.store(false,std::memory_order_relaxed);

    std::ostringstream timing;
    const bool compare_advance=mode=="1" || mode=="advance";
    const bool compare_layout=mode=="layout-bench";
    if(compare_advance || compare_layout || mode=="health" || mode=="historical"){
        // Keep each source segment below its text limit. Select distinct
        // ordinary Shift-JIS kanji from the fixture font's real ranges.
        // The grid predicts candidates; only the actual two source draws
        // determine whether a character crosses an atlas texture boundary.
        constexpr unsigned lines=3,long_length=lines*page_length;
        const auto initial_y=reveal_args[3];
        std::array<std::uint8_t,256> ranges;p.memory.copy_out(0x4a14b8,ranges);
        wa2::text::GlyphIndex index;
        std::map<std::pair<unsigned,unsigned>,unsigned> candidates;
        for(unsigned row=0;row<64;++row){
            std::uint16_t first,last;
            std::memcpy(&first,ranges.data()+row*4,2);std::memcpy(&last,ranges.data()+row*4+2,2);
            if(last==0xffff || (row && first==0xffff) || first>last)break;
            for(unsigned code=std::max<unsigned>(first,0x8980);code<=std::min<unsigned>(last,0xeafc);++code){
                const unsigned lead=code>>8,trail=code&255;
                if((lead>0x9f && lead<0xe0) || trail<0x80 || trail>0xfc)continue;
                const auto cell=index.lookup(ranges,std::uint16_t(code));CHECK(cell);
                const auto u=*cell%80*40+4,v=*cell/80*40+4;
                if(u+28>3200 || v+28>3840)continue;
                const unsigned parts=(1+unsigned(u%256+28>256))*(1+unsigned(v%256+28>256));
                if(parts<=2)candidates.try_emplace(std::pair{v/256*13+u/256,parts},code);
            }
        }
        const auto single_character=p.put_string("<C1:XX>");
        reveal_args[11]=single_character;
        std::vector<unsigned> single,crossing;
        std::set<U32> chosen_textures;
        for(const auto& [where,code]:candidates){
            const auto parts=where.second;
            auto& codes=parts==1?single:crossing;
            if(codes.size()==(parts==1?16u:4u))continue;
            p.memory.store(single_character+4,((code&255)<<8)|(code>>8),16);
            begin_page();
            const auto draws=both_passes(immediately_visible);
            if(draws[0].glyphs!=1 || draws[1].glyphs!=1 || draws[0].tiles!=parts || draws[1].tiles!=parts)continue;
            if(!chosen_textures.insert(recorded.tiles.front().texture).second)continue;
            codes.push_back(code);
            if(single.size()==16 && crossing.size()==4)break;
        }
        CHECK(single.size()==16 && crossing.size()==4);
        std::string segment="<C1:";unsigned single_count{},crossing_count{};
        for(unsigned n=0;n<page_length;++n){
            const unsigned code=n%5==4?crossing[(crossing_count++*3)%crossing.size()]:single[(single_count++*7)%single.size()];
            segment+=char(code>>8);segment+=char(code);
        }
        segment+='>';reveal_args[11]=p.put_string(segment);
        const auto long_page=[&]{
            std::uint64_t glyphs{};
            for(unsigned line=0;line<lines;++line){
                reveal_args[3]=initial_y+line*40;
                for(U32 style:{1u,0u}){
                    const auto work=frame(immediately_visible,style,line!=0 || style==0);
                    glyphs+=work.glyphs;
                    p.memory.store(cursor+4,text_result,32);
                }
            }
            CHECK(glyphs==2*long_length);
        };
        const bool saved_layout=a::text_layout_replay_enabled.load(std::memory_order_relaxed);
        if(compare_layout){
            a::text_layout_replay_enabled.store(false,std::memory_order_relaxed);
            a::compact_text_advance_enabled.store(true,std::memory_order_relaxed);
        }
        begin_page();
        long_page();
        const auto expected_tiles=recorded.tiles;
        CHECK(expected_tiles.size()==2*lines*(single_count+2*crossing_count));
        CHECK(expected_tiles.size()*10>=2*long_length*11 && expected_tiles.size()*10<=2*long_length*13);
        std::set<U32> rendered_textures;
        for(const auto& tile:expected_tiles)rendered_textures.insert(tile.texture);
        CHECK(rendered_textures.size()>=16);
        const auto expected_return=text_result;
        std::array<std::uint8_t,16> expected_cursor;p.memory.copy_out(cursor,expected_cursor);
        std::array<std::uint8_t,0x2000> expected_output;p.memory.copy_out(output,expected_output);
        constexpr unsigned pages_per_window=12;
        constexpr std::array<bool,8> windows{false,true,true,false,true,false,false,true};
        timing<<"{\"mode\":\""<<(mode=="1"?"compare":mode)<<"\",\"characters\":"<<long_length
            <<",\"source_segments\":"<<lines<<",\"glyphs_per_page\":"<<2*long_length
            <<",\"tiles_per_page\":"<<expected_tiles.size()<<",\"distinct_characters\":"<<(single.size()+crossing.size())
            <<",\"distinct_textures\":"<<rendered_textures.size()<<",\"crossing_characters_per_segment\":"<<crossing_count
            <<",\"passes_per_segment\":2,\"pages_per_window\":"<<pages_per_window<<",\"windows\":[";
        for(unsigned i=0;i<windows.size();++i){
            const bool compact=compare_layout || (compare_advance && windows[i]);
            a::compact_text_advance_enabled.store(compact,std::memory_order_relaxed);
            if(compare_layout)a::text_layout_replay_enabled.store(windows[i],std::memory_order_relaxed);
            begin_page();
            for(unsigned warm=0;warm<2;++warm)long_page();
            const auto hits_before=a::compact_text_advance_hits;
            const auto layout_before=a::text_layout_replay_hits;
            const auto start=std::chrono::steady_clock::now();const auto cpu_start=cpu_time_ns();
            for(unsigned iteration=0;iteration<pages_per_window;++iteration)long_page();
            const auto cpu_ns=cpu_time_ns()-cpu_start;
            const auto wall_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            const auto hits=a::compact_text_advance_hits-hits_before;
            const auto layout_hits=a::text_layout_replay_hits-layout_before;
            CHECK((hits>0)==compact && recorded.tiles==expected_tiles && text_result==expected_return);
            if(compare_layout)CHECK((layout_hits>0)==windows[i]);
            std::array<std::uint8_t,16> actual_cursor;p.memory.copy_out(cursor,actual_cursor);
            std::array<std::uint8_t,0x2000> actual_output;p.memory.copy_out(output,actual_output);
            CHECK(actual_cursor==expected_cursor && actual_output==expected_output);
            if(i)timing<<',';
            timing<<"{\"compact_advance\":"<<(compact?"true":"false")<<",\"cpu_ns\":"<<cpu_ns
                <<",\"wall_ns\":"<<wall_ns<<",\"hits\":"<<hits;
            if(compare_layout)timing<<",\"layout_replay\":"<<(windows[i]?"true":"false")<<",\"layout_hits\":"<<layout_hits;
            timing<<'}';
        }
        timing<<"]}";
        if(compare_layout)a::text_layout_replay_enabled.store(saved_layout,std::memory_order_relaxed);
        a::compact_text_advance_enabled.store(false,std::memory_order_relaxed);
        reveal_args[3]=initial_y;
        reveal_args[11]=reveal_string;
    }
    // The new allowance is finite and independent of the generic call budget.
    unsigned guard_cases{};
    {wr::Memory other;CHECK(!a::GlyphCallScope::remaining(p.memory));
        a::GlyphCallScope outer(p.memory,999);auto* remaining=a::GlyphCallScope::remaining(p.memory);
        CHECK(remaining && *remaining==256 && !a::GlyphCallScope::remaining(other));++guard_cases;
        --*remaining;
        {a::GlyphCallScope nested(other,2);CHECK(!a::GlyphCallScope::remaining(p.memory));CHECK(*a::GlyphCallScope::remaining(other)==2);++guard_cases;}
        CHECK(a::GlyphCallScope::remaining(p.memory)==remaining && *remaining==255);++guard_cases;
        {a::GlyphCallScope nested(p.memory,1);CHECK(*a::GlyphCallScope::remaining(p.memory)==1);++guard_cases;}
        CHECK(a::GlyphCallScope::remaining(p.memory)==remaining && *remaining==255);++guard_cases;
    }
    CHECK(!a::GlyphCallScope::remaining(p.memory));++guard_cases;
    auto cpu=initial_cpu;cpu.r[wr::ESP]-=0x4000;cpu.fp.put16(0,0x7f);cpu.eip=0x4462e0;
    const auto sp=cpu.r[wr::ESP];p.memory.store(sp,0xf0001234u,32);p.memory.copy_in(sp+4,sprite);
    const std::array<U32,9> glyph_args{'A',40,40,68,68,4,28,0xffffffffu,0};
    for(unsigned n=0;n<glyph_args.size();++n)p.memory.store(sp+0x104+n*4,glyph_args[n],32);
    const auto check_unchanged=[&](const wr::Cpu& before,const std::array<std::uint8_t,0x128>& stack,std::size_t tiles){
        CHECK(cpu.eip==before.eip && cpu.r==before.r && cpu.fp.bytes==before.fp.bytes && cpu.flags==before.flags && cpu.defined_flags==before.defined_flags);
        std::array<std::uint8_t,0x128> after;p.memory.copy_out(sp,after);CHECK(after==stack && recorded.tiles.size()==tiles);++guard_cases;
    };
    {a::SpriteService sink(p.memory,Recording::submit,&recorded);
        a::NativeCallScope generic(nullptr,nullptr);CHECK(a::current_native_calls->remaining==32);
        a::current_native_calls->remaining=0;
        {a::GlyphCallScope glyphs(p.memory);
            for(unsigned n=0;n<256;++n){cpu.eip=0x4462e0;cpu.r[wr::ESP]=sp;CHECK(a::native_draw_glyph(cpu,p.memory));CHECK(!a::current_native_calls->remaining);}
            CHECK(!*a::GlyphCallScope::remaining(p.memory));++guard_cases;
            cpu.eip=0x4462e0;cpu.r[wr::ESP]=sp;const auto before=cpu;std::array<std::uint8_t,0x128> stack;p.memory.copy_out(sp,stack);
            const auto tiles=recorded.tiles.size(),exhaustions=a::glyph_budget_counts.exhaustions;
            CHECK(!a::native_draw_glyph(cpu,p.memory));CHECK(a::glyph_budget_counts.exhaustions==exhaustions+1);check_unchanged(before,stack,tiles);
        }
        // Calls outside a Program glyph scope keep the previous generic limit.
        const auto before=cpu;std::array<std::uint8_t,0x128> stack;p.memory.copy_out(sp,stack);const auto tiles=recorded.tiles.size();
        CHECK(!a::native_draw_glyph(cpu,p.memory));check_unchanged(before,stack,tiles);
        a::current_native_calls->remaining=1;CHECK(a::native_draw_glyph(cpu,p.memory));CHECK(!a::current_native_calls->remaining);++guard_cases;
        {a::GlyphCallScope glyphs(p.memory,1);
            unsigned declines{};a::SpriteService decline(p.memory,[](const a::SpriteCommand&,void* owner){++*static_cast<unsigned*>(owner);return false;},&declines);
            cpu.eip=0x4462e0;cpu.r[wr::ESP]=sp;const auto before=cpu;std::array<std::uint8_t,0x128> stack;p.memory.copy_out(sp,stack);const auto tiles=recorded.tiles.size();
            CHECK(!a::native_draw_glyph(cpu,p.memory) && declines==1 && *a::GlyphCallScope::remaining(p.memory)==1);
            check_unchanged(before,stack,tiles);
        }
    }
    unsigned handle_guards{};
    {a::SpriteService sink(p.memory,Recording::submit,&recorded);a::GlyphCallScope glyphs(p.memory);
        wa2::text::GlyphIndex index;std::array<std::uint8_t,256> table;p.memory.copy_out(0x4a14b8,table);
        const auto resolved=index.lookup(table,'A');CHECK(resolved);
        const auto tile_index=((*resolved/80*40+4)/256)*13+(*resolved%80*40+4)/256;
        const auto slot=data+0x4000+tile_index*4,original=p.memory.load(slot,32);
        for(auto value:{original+16,0u,original}){
            p.memory.store(slot,value,32);cpu.eip=0x4462e0;cpu.r[wr::ESP]=sp;recorded.reset();
            CHECK(a::native_draw_glyph(cpu,p.memory));
            if(value)CHECK(recorded.tiles.size()==1 && recorded.tiles[0].texture==value);else CHECK(recorded.tiles.empty());
            ++handle_guards;
        }
        // The selected word is readable but an UNUSED suffix is protected.
        // Retain the former full-table guard before admitting a lazy read.
        const auto font=0xa93fb8u,old_pointer=p.memory.load(font+0x70,32),pointer=data+0x5000-4-tile_index*4;
        p.memory.store(pointer+tile_index*4,original,32);p.memory.store(font+0x70,pointer,32);
        p.memory.protect(data+0x5000,4096,0);
        cpu.eip=0x4462e0;cpu.r[wr::ESP]=sp;auto before=cpu;std::array<std::uint8_t,0x128> stack;p.memory.copy_out(sp,stack);auto tiles=recorded.tiles.size();
        CHECK(!a::native_draw_glyph(cpu,p.memory));check_unchanged(before,stack,tiles);++handle_guards;
        p.memory.protect(data+0x5000,4096,wr::Memory::Read|wr::Memory::Write);
        p.memory.store(font+0x70,0xfffffffcu,32);before=cpu;
        CHECK(!a::native_draw_glyph(cpu,p.memory));check_unchanged(before,stack,tiles);++handle_guards;
        p.memory.store(font+0x70,old_pointer,32);
    }
    const auto write_work=[&](const RevealWork& work){
        report<<"{\"glyphs\":"<<work.glyphs<<",\"tiles\":"<<work.tiles
            <<",\"single_handle_reads\":"<<work.single_handle_reads<<",\"local_jumps\":"<<work.jumps<<'}';
    };
    report<<"],\"reveal_paths\":{\"immediate\":";write_work(direct);
    report<<",\"finite_full\":";write_work(direct_finite);
    report<<",\"finite_saturated\":";write_work(saturated);
    report<<",\"progression\":[";
    for(std::size_t i=0;i<stages.size();++i){if(i)report<<',';write_work(stages[i]);}
    report<<"],\"styled_then_plain\":[";
    write_work(direct_pair[0]);report<<',';write_work(direct_pair[1]);
    report<<"],\"styled_and_plain_progression_glyphs\":[";
    for(std::size_t i=0;i<paired_stages.size();++i){
        if(i)report<<',';
        report<<paired_stages[i][0].glyphs+paired_stages[i][1].glyphs;
    }
    report<<"],\"two_pass_order_matches_static_reference\":true,\"alpha_only_tile_differences\":"<<partially_faded
        <<",\"same_limit_same_work_and_vertices\":true,\"models_save_loading\":false,\"models_script_progression\":false}";
    report<<",\"compact_text_advance\":{\"paired_page_hits\":"<<advance_hits<<'}';
    if(layout_cases)report<<",\"layout_state_oracle_cases\":"<<layout_cases;
    if(!timing.str().empty())report<<",\"two_pass_timing\":"<<timing.str();
    report<<",\"exact_full_text_cases\":"<<exact_pages<<",\"formatted_pages\":"<<formatted_pages<<",\"unchanged_page_checks\":"<<unchanged_page_checks<<",\"guard_cases\":"<<guard_cases<<",\"handle_guard_cases\":"<<handle_guards
        <<",\"maximum_glyphs_per_boundary\":256,\"generic_call_limit_unchanged\":true,\"nested_memory_scopes_checked\":true,\"exhaustion_fallback_preserves_state\":true"
        <<",\"exact_texture_order_vertices_output_and_abi\":true,\"complete_handle_range_still_checked\":true,\"handle_changes_visible_immediately\":true,\"game_launched\":false,\"screen_access\":false,\"source_executable_opened\":false,\"passed\":true}\n";
    CHECK(report && snapshot);std::cout<<"Finite text helper and budget checks passed.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
