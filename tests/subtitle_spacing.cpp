// Own subtitle documents, synthetic glyphs and an off-screen font adapter.
// No game, window, GL context, screen pixels or original resources are used.
#include "../engine/external_subtitles.hpp"
#include "../platform/subtitle_font.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": "+#x);}while(false)
namespace s=wa2::subtitles;
unsigned checks{},guards{},cases{},fixed_cues{},adapter_cases{};
std::span<const std::uint8_t> bytes(const std::string& text){return {reinterpret_cast<const std::uint8_t*>(text.data()),text.size()};}
template<class F>void rejects(F action){bool caught{};try{action();}catch(const std::exception&){caught=true;}CHECK(caught);++guards;}
std::vector<s::GlyphMask> raster(std::span<const char32_t> characters){
    std::vector<s::GlyphMask> out;
    for(const auto cp:characters){
        const float advance=cp==U' '?7.f:cp==U'W'?26.f:cp==U'M'?24.f:(cp==U'i' || cp==U'l' || cp==0x456)?6.f:cp==U'.'?7.f:cp==0x301?0.f:cp>=0x2e80?28.f:14.f;
        const unsigned width=cp==U' '?0u:cp==0x301?3u:unsigned(std::max(1.f,advance-2));
        s::GlyphMask mask{cp==0x301?-4:0,-24,width,cp==U' '?0u:26u,{},advance};
        mask.coverage.assign(std::size_t(mask.width)*mask.height,255);out.push_back(std::move(mask));
    }return out;
}
std::shared_ptr<const s::Catalogue> compile(const std::string& text,bool legacy=false){
    const auto source=legacy?"{\n0 8365 2001 5\n0 1000 "+text+"\n}\n":
        "soundEffect:\n    - id: 8365\n      content:\n          - begin: 0\n            end: 1000\n            text: "+text+"\nvoice:\n";
    auto out=s::compile_external(s::parse_external(bytes(source)),raster);
    CHECK(out->atlas() && out->metrics().size()==out->glyph_count());return out;
}
std::vector<s::GlyphQuad> render(const std::shared_ptr<const s::Catalogue>& catalogue){
    s::Playback playback;playback.catalogue(catalogue);CHECK(playback.play(8365,s::Kind::effect,0,100));
    const auto* cue=playback.current(100);CHECK(cue && cue==playback.current(1099) && !playback.current(1100));
    CHECK(playback.font().advance==28);return playback.layout(*cue);
}
void same(const std::vector<s::GlyphQuad>& a,const std::vector<s::GlyphQuad>& b){
    CHECK(a.size()==b.size());for(unsigned n=0;n<a.size();++n){const auto& x=a[n];const auto& y=b[n];CHECK(x.left==y.left && x.top==y.top && x.right==y.right && x.bottom==y.bottom && x.u0==y.u0 && x.v0==y.v0 && x.u1==y.u1 && x.v1==y.v1);}
}
int main(int argc,char** argv){try{
    CHECK(argc==2);
    for(bool legacy:{false,true}){
        auto catalogue=compile("Wi iW",legacy);const auto q=render(catalogue);const auto font=catalogue->font();
        CHECK(q.size()==5 && q[1].left-q[0].left==26 && q[2].left-q[1].left==6 && q[3].left-q[2].left==7 && q[4].left-q[3].left==6);
        CHECK(q.front().left+font.origin_x+71.f/2==640.f);for(const auto& glyph:q)CHECK(glyph.right-glyph.left==font.cell && glyph.bottom-glyph.top==font.cell);++cases;
        catalogue=compile(std::string(30,'W')+" "+std::string(30,'W'),legacy);const auto words=render(catalogue);
        CHECK(words.size()==60 && words[29].top==words[0].top && words[30].top==words[0].top+36 && words.back().top==words[30].top);
        for(unsigned at:{0u,30u})CHECK(words[at].left+catalogue->font().origin_x+780.f/2==640.f);++cases;
        catalogue=compile(std::string(80,'W'),legacy);const auto long_word=render(catalogue);
        CHECK(long_word.size()==80 && long_word[45].top==long_word[0].top && long_word[46].top==long_word[0].top+36);++cases;
        catalogue=compile(std::string(512,'i'),legacy);const auto narrow=render(catalogue);
        CHECK(narrow.size()==512 && narrow[201].top==narrow[0].top && narrow[202].top==narrow[0].top+36);++cases;
    }
    {auto catalogue=compile("Wi^iW",true);const auto q=render(catalogue);CHECK(q.size()==4 && q[2].top==q[0].top+36 && q[2].left==q[0].left);++cases;}
    {auto catalogue=compile("Wi^^iW",true);const auto q=render(catalogue);CHECK(q.size()==4 && q[2].top==q[0].top+72);++cases;}
    {auto catalogue=compile("Привіт світе",true);const auto q=render(catalogue);CHECK(!q.empty());++cases;}
    const std::string feedback="Sample: Can we finish this announcement and leave enough space between the words for everyone to read it?";
    auto feedback_catalogue=compile(feedback,true);const auto& feedback_cue=feedback_catalogue->tracks().front().cues.front();
    float feedback_width{};for(auto id:feedback_cue.glyphs)feedback_width+=feedback_catalogue->metrics()[id].advance;
    CHECK(feedback_width<float(feedback.size()*28)*.7f);const auto feedback_quads=render(feedback_catalogue);
    std::set<float> rows;for(const auto& q:feedback_quads)rows.insert(q.top);CHECK(rows.size()<=2);++cases;
    // The existing Chinese source retains its original atlas, IDs and every
    // quad; no platform font is needed to display any of its 315 cues.
    const auto source=std::filesystem::path(__FILE__).parent_path().parent_path()/"third_party/wa2_subtitles/sub.yaml";
    std::ifstream input(source,std::ios::binary);CHECK(input);std::string original((std::istreambuf_iterator<char>(input)),{});
    auto chinese=s::compile_external(s::parse_yaml(bytes(original)),[](auto)->std::vector<s::GlyphMask>{throw std::runtime_error("Chinese atlas must be reused");});
    CHECK(!chinese->atlas() && chinese->metrics().empty());s::Playback old;old.catalogue(chinese);
    for(const auto& track:chinese->tracks())for(const auto& cue:track.cues){same(old.layout(cue),s::layout(cue,s::font_spec(),s::characters().size()));++fixed_cues;}
    CHECK(fixed_cues==315);
    const auto& cue=feedback_cue;const auto font=feedback_catalogue->font();const auto glyphs=feedback_catalogue->glyph_count();
    auto metrics=std::vector<s::GlyphMetric>(feedback_catalogue->metrics().begin(),feedback_catalogue->metrics().end());
    same(s::layout(cue,font,glyphs),s::layout(cue,font,glyphs,{}));
    for(float invalid:{-1.f,41.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        auto bad=metrics;bad[0].advance=invalid;rejects([&]{s::layout(cue,font,glyphs,bad);});
        const std::array<char32_t,1> cp{U'W'};auto masks=raster(cp);masks[0].advance=invalid;rejects([&]{s::compose_atlas(masks);});
    }
    rejects([&]{s::layout(cue,font,glyphs,std::span(metrics).first(metrics.size()-1));});
    {auto bad=font;bad.advance=0;rejects([&]{s::layout(cue,bad,glyphs,metrics);});}
    {const std::array<std::uint16_t,1> invalid{std::uint16_t(glyphs)};rejects([&]{s::layout({0,1,invalid},font,glyphs,metrics);});}
    // Combining marks may legitimately have zero advance.
    {auto catalogue=compile("íW",true);const auto q=render(catalogue);CHECK(q.size()==3 && q[2].left-q[1].left==0);++cases;}
    float native_i{},native_w{},native_space{};
#ifdef _WIN32
    const std::array<char32_t,3> cp{U'i',U'W',U' '};const auto masks=wa2::platform::subtitle_glyphs(cp);
    CHECK(masks.size()==3 && masks[0].advance>0 && masks[0].advance<masks[1].advance && masks[2].advance>0);
    CHECK(masks[2].coverage.empty());native_i=masks[0].advance;native_w=masks[1].advance;native_space=masks[2].advance;adapter_cases=3;
#endif
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.subtitle-spacing.v1\",\"checks\":"<<checks<<",\"layout_cases\":"<<cases<<",\"guards\":"<<guards<<",\"unchanged_chinese_cues\":"<<fixed_cues<<",\"native_font_metric_cases\":"<<adapter_cases<<",\"native_advances\":["<<native_i<<','<<native_w<<','<<native_space<<"],\"sample_text_monospace_width\":"<<feedback.size()*28<<",\"sample_text_measured_width\":"<<feedback_width<<",\"sample_text_rows\":"<<rows.size()<<",\"font_size_unchanged\":true,\"word_wrap_and_explicit_newlines\":true,\"zero_advance_marks_checked\":true,\"yaml_and_todokanai_checked\":true,\"playback_layout_used\":true,\"game_launched\":false,\"screen_access\":false,\"source_executable_opened\":false,\"passed\":true}\n";CHECK(report);std::cout<<"PASS measured subtitle advances, spaces, centring, wrapping and unchanged embedded Chinese.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
