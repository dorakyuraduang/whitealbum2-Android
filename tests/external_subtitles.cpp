#include "../engine/external_subtitles.hpp"
#include "../recompiled/process.hpp"
#include "../recompiled/subtitles.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace s=wa2::subtitles;
unsigned checks{},rejections{},exact_cues{},exact_glyphs{};
std::span<const std::uint8_t> bytes(const std::string& text){return {reinterpret_cast<const std::uint8_t*>(text.data()),text.size()};}
template<class F>void rejects(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}CHECK(failed);++rejections;}
std::string replace(std::string value,std::string_view old,std::string_view next){auto at=value.find(old);CHECK(at!=value.npos);value.replace(at,old.size(),next);return value;}
std::string encoded(char32_t cp){std::string text;if(cp<128)text+=char(cp);else if(cp<2048){text+=char(0xc0|(cp>>6));text+=char(0x80|(cp&63));}else if(cp<65536){text+=char(0xe0|(cp>>12));text+=char(0x80|((cp>>6)&63));text+=char(0x80|(cp&63));}else{text+=char(0xf0|(cp>>18));text+=char(0x80|((cp>>12)&63));text+=char(0x80|((cp>>6)&63));text+=char(0x80|(cp&63));}return text;}
const std::string small="soundEffect:\n    - id: 8365\n      content:\n          - begin: 0\n            end: 5000\n            text: 峰城广播站\nvoice:\n";
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto source=std::filesystem::path(__FILE__).parent_path().parent_path()/"third_party/wa2_subtitles/sub.yaml";std::ifstream file(source,std::ios::binary);CHECK(file);std::string pinned((std::istreambuf_iterator<char>(file)),{});
    const auto doc=s::parse_yaml(bytes(pinned));CHECK(doc.tracks.size()==46 && doc.cue_count==315 && doc.text_count==6462 && doc.characters.size()==891);
    unsigned font_calls{};const auto catalogue=s::compile_external(doc,[&](auto)->std::vector<s::GlyphMask>{++font_calls;throw std::runtime_error("Embedded text must reuse its original bitmap");});CHECK(!font_calls && !catalogue->atlas());
    const auto compare=[&](const auto& candidate){const auto reference=s::main_story_tracks();CHECK(candidate->tracks().size()==reference.size());for(std::size_t n=0;n<reference.size();++n){const auto& actual=candidate->tracks()[n];CHECK(actual.key==reference[n].key && actual.cues.size()==reference[n].cues.size());for(std::size_t q=0;q<actual.cues.size();++q){const auto& a=actual.cues[q];const auto& b=reference[n].cues[q];CHECK(a.begin_ms==b.begin_ms && a.end_ms==b.end_ms && std::equal(a.glyphs.begin(),a.glyphs.end(),b.glyphs.begin(),b.glyphs.end()));}}};compare(catalogue);
    for(const auto& track:catalogue->tracks())for(const auto& cue:track.cues){++exact_cues;exact_glyphs+=unsigned(cue.glyphs.size());}
    std::string crlf="\xef\xbb\xbf";for(char c:pinned){if(c=='\n' && (crlf.empty() || crlf.back()!='\r'))crlf+='\r';crlf+=c;}compare(s::compile_external(s::parse_yaml(bytes(crlf))));
    auto simple=s::compile_external(s::parse_yaml(bytes(small)));CHECK(simple->tracks().size()==1 && simple->find(8365));CHECK(!simple->find(0));
    s::Playback player;player.catalogue(simple);CHECK(player.font_revision()==1 && player.tracks().size()==1);CHECK(player.play(8365,s::Kind::effect,2,100));CHECK(player.current(5099) && !player.current(5100));
    CHECK(!player.play(s::main_story_tracks().back().key,s::Kind::voice,0,100));player.enabled(false);CHECK(!player.play(8365,s::Kind::effect,0,100));player.enabled(true);
    player.catalogue(nullptr);CHECK(player.font_revision()==2 && player.tracks().size()==46 && !player.current(100));
    auto custom=replace(small,"begin: 0","begin: 200");custom=replace(custom,"end: 5000","end: 700");auto custom_data=s::compile_external(s::parse_yaml(bytes(custom)));
    wa2::aot::Runtime runtime({}, {}, {},true,custom_data);auto bound=wa2::aot::find_source_subtitles(runtime.process().memory);CHECK(bound && bound->tracks().size()==1 && bound->font_revision()==1);
    auto& ram=runtime.process().memory;ram.map(0x2000,4096,wr::Memory::Read|wr::Memory::Write);ram.store(0x2000,3,32);ram.store(0x2004,8365,32);
    wa2::aot::source_subtitle_return(*bound,0x40f400,runtime.process().cpu,ram,0x2000,1000);CHECK(!bound->current(1199) && bound->current(1200) && !bound->current(1700));
    const auto unknown=replace(small,"峰城广播站",encoded(0x1f642));auto unknown_doc=s::parse_yaml(bytes(unknown));rejects([&]{s::compile_external(unknown_doc);});
    auto rendered=s::compile_external(unknown_doc,[](auto chars){std::vector<s::GlyphMask> masks;for(auto c:chars){(void)c;masks.push_back({0,-2,2,2,{0,128,255,64}});}return masks;});CHECK(rendered->atlas() && rendered->glyph_count()==1);
    player.catalogue(rendered);CHECK(player.glyph_count()==1 && player.font().advance==28 && player.font_image().rgba==rendered->atlas()->rgba);
    const auto& image=*rendered->atlas();unsigned transparent{},black{},white{},partial{};for(std::size_t n=0;n<image.rgba.size();n+=4){CHECK(image.rgba[n]==image.rgba[n+1] && image.rgba[n+1]==image.rgba[n+2]);const auto alpha=image.rgba[n+3];transparent+=!alpha;black+=alpha && !image.rgba[n];white+=alpha && image.rgba[n]==255;partial+=alpha>0 && alpha<255;}CHECK(transparent && black && white && partial);
    rejects([&]{s::compile_external(unknown_doc,[](auto){return std::vector<s::GlyphMask>{};});});
    for(auto bad:std::vector<std::string>{"", "voice:\n", "soundEffect:\nvoice:\n",small+"specialContents:\n",small+"voice:\n",replace(small,"8365","65536"),replace(small,"8365","-1"),replace(small,"begin: 0","begin: -1"),replace(small,"end: 5000","end: 4294967296"),replace(small,"end: 5000","end: 0"),replace(small,"begin: 0","begin: 5001"),replace(small,"end: 5000\n",""),replace(small,"text: 峰城广播站","text: !object"),replace(small,"text: 峰城广播站","text: &anchor"),replace(small,"text: 峰城广播站","text: [array]"),replace(small,"text: 峰城广播站","text: |"),replace(small,"text: 峰城广播站","text: \"quoted\""),replace(small,"text: 峰城广播站","text: "),replace(small,"content:","unknown:"),replace(small,"      content:","\tcontent:"),replace(small,"            end: 5000","            end: 5000\n            end: 6000"),replace(small,"            text: 峰城广播站","            text: 峰城广播站\n            text: 峰城广播站"),replace(small,"voice:","    - id: 8365\n      content:\n          - begin: 1\n            end: 3\n            text: 峰\nvoice:"),replace(small,"voice:","          - begin: 4999\n            end: 6000\n            text: 峰\nvoice:"),replace(small,"8365","3.14"),replace(small,"8365","0x20"),replace(small,"峰城广播站",std::string(1,char(0xff))),replace(small,"峰城广播站",std::string("\xed\xa0\x80",3)),replace(small,"峰城广播站",std::string("\xc0\xaf",2)),replace(small,"峰城广播站",std::string("\xf4\x90\x80\x80",4)),replace(small,"峰城广播站",std::string("\0",1)),replace(small,"峰城广播站",std::string("a\tb")),replace(small,"峰城广播站",std::string(513,'a'))})rejects([&]{s::parse_yaml(bytes(bad));});
    const auto voice="soundEffect:\nvoice:\n    - scene: 1001\n      id: 10\n      content:\n          - begin: 0\n            end: 100\n            text: 峰\n";
    CHECK(s::parse_yaml(bytes(voice)).tracks[0].key==((1001u<<16)|10));for(auto bad:{replace(voice,"scene: 1001","scene: 0"),replace(voice,"scene: 1001","scene: 65536"),replace(voice,"      id: 10\n",""),replace(voice,"      id: 10","      id: 10\n      id: 11")})rejects([&]{s::parse_yaml(bytes(bad));});
    std::string huge(s::maximum_yaml_bytes+1,' ');rejects([&]{s::parse_yaml(bytes(huge));});
    auto overlong=replace(small,"峰城广播站",std::string(512,'a'));CHECK(s::parse_yaml(bytes(overlong)).text_count==512);
    std::string tracks="soundEffect:\n";for(unsigned n=0;n<1025;++n)tracks+="    - id: "+std::to_string(n)+"\n      content:\n          - begin: 0\n            end: 1\n            text: 峰\n";tracks+="voice:\n";rejects([&]{s::parse_yaml(bytes(tracks));});
    std::string cues="soundEffect:\n    - id: 1\n      content:\n";for(unsigned n=0;n<8193;++n)cues+="          - begin: "+std::to_string(n*2)+"\n            end: "+std::to_string(n*2+1)+"\n            text: 峰\n";cues+="voice:\n";rejects([&]{s::parse_yaml(bytes(cues));});
    std::string many="soundEffect:\n    - id: 1\n      content:\n";for(unsigned n=0;n<257;++n)many+="          - begin: "+std::to_string(n*2)+"\n            end: "+std::to_string(n*2+1)+"\n            text: "+std::string(512,'a')+"\n";many+="voice:\n";rejects([&]{s::parse_yaml(bytes(many));});
    std::string chars="soundEffect:\n    - id: 1\n      content:\n";for(unsigned n=0;n<4;++n){chars+="          - begin: "+std::to_string(n*2)+"\n            end: "+std::to_string(n*2+1)+"\n            text: ";for(unsigned k=0;k<(n<3?512u:1u);++k)chars+=encoded(0x3400+n*512+k);chars+='\n';}chars+="voice:\n";rejects([&]{s::parse_yaml(bytes(chars));});
    rejects([&]{s::compose_atlas({});});s::GlyphMask invalid{0,0,37,1,std::vector<std::uint8_t>(37,255)};rejects([&]{s::compose_atlas({&invalid,1});});invalid={0,0,1,1,{}};rejects([&]{s::compose_atlas({&invalid,1});});
    auto broken=doc;broken.text_count=1;rejects([&]{s::compile_external(broken);});broken=doc;broken.cue_count=0;rejects([&]{s::compile_external(broken);});
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.external-subtitles.v1\",\"checks\":"<<checks<<",\"rejections\":"<<rejections<<",\"exact_embedded_cues\":"<<exact_cues<<",\"exact_embedded_glyphs\":"<<exact_glyphs<<",\"bom_crlf_checked\":true,\"shared_audio_binding_checked\":true,\"external_replaces_embedded_tracks\":true,\"new_character_atlas_checked\":true,\"legacy_bitmap_reused\":true,\"bounded_yaml_only\":true,\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(report);
    std::cout<<"PASS external sub.yaml: complete embedded-source comparison, strict parser guards, lifetime/timers and native font atlas.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
