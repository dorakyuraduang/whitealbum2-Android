#include "../engine/external_subtitles.hpp"
#include "../platform/compat/language_resources.hpp"
#include "../platform/compat/resource_mount.hpp"
#include "../recompiled/language_names.hpp"
#include "../recompiled/language_prompts.hpp"
#include "../recompiled/language_text.hpp"
#include "../recompiled/process.hpp"
#include "../recompiled/subtitles.hpp"
#include "../port/native_resources.hpp"
#include <winrecomp/win32_state.hpp>
#include <array>
#include <algorithm>
#include <map>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <tuple>
namespace a=wa2::aot;namespace r=wa2::resource;namespace s=wa2::subtitles;namespace pc=wa2::platform::compat;
unsigned checks{},guards{},resource_cases{},policy_cases{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);++guards;}
std::span<const std::uint8_t> bytes(const std::string& text){return {reinterpret_cast<const std::uint8_t*>(text.data()),text.size()};}
std::vector<std::uint8_t> pak(const std::vector<std::pair<std::string,unsigned>>& entries){
    std::vector<std::uint8_t> out(16+entries.size()*45);std::copy_n("KCAP",4,out.begin());
    const auto word=[&](unsigned at,unsigned value){for(unsigned n=0;n<4;++n)out[at+n]=std::uint8_t(value>>(n*8));};word(12,unsigned(entries.size()));
    for(unsigned n=0;n<entries.size();++n){const auto& [name,marker]=entries[n];CHECK(name.size()<=24);std::copy(name.begin(),name.end(),out.begin()+16+n*44+4);word(16+n*44+36,unsigned(16+entries.size()*44+n));word(16+n*44+40,marker?1:0);out[16+entries.size()*44+n]=std::uint8_t(marker);}
    return out;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto root=std::filesystem::path(argv[1]).parent_path()/("ua-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(root);
    const std::vector<std::string> fonts{"\x96\x7b\x91\xcc" "80.tga","\x91\xdc\x89\x65" "80.tga","14pt\x96\x7b\x91\xcc.tga","14pt\x91\xdc.tga"};
    const std::map<std::string,std::vector<std::uint8_t>> files{
        {"ua.pak",pak({{"bak",0},{"same.tga",0x61},{"grp",0},{"same.tga",0x62},{"script",0},{"1001.bnr",0x31},{"1001.txt",0x32},{"ic/script",0},{"1001.bnr",0x41},{"1001.txt",0x42}})},
        {"font_ua.pak",pak({{fonts[0],0x51},{fonts[1],0x52},{fonts[2],0x53},{fonts[3],0x54}})},
        {"script.pak",pak({{"Global.vrb",0x71}})},{"IC/script.pak",pak({{"Global.vrb",0x72}})},
        {"ck-gal.pak",pak({{"1001.txt",0x11}})},{"IC/ck-gal.pak",pak({{"1001.txt",0x12}})},
        {"fnt.pak",pak({{fonts[0],0x21}})},{"patch.pak",pak({{"1001.txt",0x81}})}};
    std::vector<wr::ReadOnlyFileInfo> entries;
    for(const auto& [name,data]:files){auto path=root/name;std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),data.size());out.close();CHECK(out);entries.push_back({name,data.size(),1,0x21});}
    for(const auto* folder:{"ua","font_ua","IC/ua","IC/font_ua"}){std::filesystem::create_directories(root/folder);std::ofstream(root/folder/"loose.bin").put(char(0x91));}
    std::filesystem::create_directory(root/"ck-gal");std::ofstream(root/"ck-gal"/"chinese-only.bin").put('c');
    const auto mount=pc::local_resource_mount(root);unsigned loose_aliases{};
    for(bool linear:{false,true}){
        wr::ProcessOptions options;options.data_root=root;options.prefer_linear_memory=linear;
        wr::Process p(wr::FunctionExecutor{[](auto&,auto&){throw std::runtime_error("No source instructions in resource checks");return false;}},options);
        wr::U32 target{};p.replace_api("kernel32.dll","HeapAlloc",3,[&](auto&){return target;});
        wr::install_wa2_native_resources(p);pc::install_resource_mount(p,mount);pc::select_language_resources(p,{r::GameLanguage::ukrainian,true});
        for(const auto* path:{"ck-gal/loose.bin","fon/loose.bin","IC/ck-gal/loose.bin","IC/fon/loose.bin"}){auto info=p.state().read_only_files->info(path);CHECK(info && info->size==1);std::unique_ptr<FILE,decltype(&std::fclose)> input(p.state().read_only_files->open(path),std::fclose);CHECK(input && std::fgetc(input.get())==0x91);++loose_aliases;}
        CHECK(!p.state().read_only_files->info("ck-gal/chinese-only.bin"));++loose_aliases;
        p.load(a::initial_image());
        target=p.allocate_bytes(4096);const auto output=p.allocate_bytes(4096);
        const std::array<std::tuple<std::string,std::string,unsigned>,9> cases{{
            {"ck-gal.pak","1001.txt",0x32},{"IC/ck-gal.pak","1001.txt",0x42},
            {"script.pak","Global.vrb",0x71},{"IC/script.pak","Global.vrb",0x72},
            {"fnt.pak",fonts[0],0x51},{"fon.pak",fonts[2],0x53},
            {"bak.pak","same.tga",0x61},{"grp.pak","same.tga",0x62},{"patch.pak","1001.txt",0x81}}};
        for(const auto& [file,name,expected]:cases){const auto path=p.put_string(file),member=p.put_string(name);
            CHECK(p.callback(0x437220,std::array<wr::U32,2>{path,member},false));
            CHECK(p.callback(0x437310,std::array<wr::U32,3>{path,member,output},false)==1 && p.memory.load(target,8)==expected);++resource_cases;}
        CHECK(!p.executed_instructions);
        a::Runtime runtime(options);runtime.load(a::initial_image());auto& q=runtime.process();const auto epoch=q.memory.code_epoch();
        a::configure_language_names(q,r::GameLanguage::ukrainian);a::configure_language_prompts(q,r::GameLanguage::ukrainian);
        const std::map<unsigned,std::string_view> names{{0,"Uaqtk'"},{1,"Kaehtra"},{2,"Rf+tna"},{10,"Sakfz"},{11,"V'ka}'"},{12,";o"},{16,"Joko"}};
        for(const auto& name:a::language_default_names()){const auto expected=names.contains(name.slot)?names.at(name.slot):name.japanese;CHECK(q.read_string(q.memory.load(0x4be348+name.slot*4,32),64)==expected);}
        for(const auto& prompt:a::language_menu_prompts())CHECK(!prompt.ukrainian.empty() && q.read_string(prompt.address,64)==prompt.ukrainian && q.memory.permissions(prompt.address)==wr::Memory::Read);
        CHECK(q.state().reports.contains("ukrainian_text") && q.memory.code_epoch()==epoch && !q.executed_instructions && !q.native_function_entries);
        const auto stack=q.allocate_bytes(4096),text=q.allocate_bytes(4096);q.memory.store(stack+0x20,text,32);wr::Memory other;
        CHECK(a::source_quote_byte(q.memory)==0x27);CHECK(a::source_text_advance(q.cpu,q.memory,0xffffffff,0,1)==1);
        {a::SourceTextScope scope(q.memory,true);CHECK(a::source_quote_byte(q.memory)==0x60 && a::source_quote_byte(other)==0x27);
            for(unsigned code=32;code<128;++code)for(auto value:{0u,4u,5u,6u,100u,0x80000000u,0xffffffffu}){
                q.memory.store(text+7,code,8);q.cpu.flags=0x8d7;q.cpu.defined_flags=0x123;auto expected=q.cpu;
                const unsigned amount=code==0x22?5u:(code==0x27 || code==0x3b || code==0x60)?6u:0u;
                // Reference the PC trampoline's CMP/CMP/CMP/CMP/SUB flow.
                (void)wr::sub(expected,code,0x22,8);
                if(code!=0x22){(void)wr::sub(expected,code,0x27,8);if(code!=0x27){(void)wr::sub(expected,code,0x3b,8);if(code!=0x3b)(void)wr::sub(expected,code,0x60,8);}}
                const auto wanted=amount?wr::sub(expected,value,amount,32):value;
                CHECK(a::source_text_advance(q.cpu,q.memory,stack,7,value)==wanted && q.cpu.flags==expected.flags && q.cpu.defined_flags==expected.defined_flags);++policy_cases;
            }
            {a::SourceTextScope nested(other,false);CHECK(a::source_quote_byte(q.memory)==0x27);}CHECK(a::source_quote_byte(q.memory)==0x60);
            rejects([&]{a::source_text_advance(q.cpu,q.memory,0xffff0000,0,1);});
        }CHECK(a::source_quote_byte(q.memory)==0x27);
    }
    CHECK(r::parse_language("uk-UA")==r::GameLanguage::ukrainian && r::language_profile(r::GameLanguage::ukrainian).font=="font_ua.pak");
    const std::string legacy="{\r\n1002 9763 1002 114\r\n0 800 Hello^world.\r\n700 900 Привіт!\r\n}\r\n{\n1008 166 1008 295\n0 200 Test\n}\n{\n0 8365 2001 5\n0 500 Station\n}\n";
    const auto document=s::parse_external(bytes(legacy));CHECK(document.tracks.size()==3 && document.cue_count==4 && document.legacy_audio_scene_matching);
    const auto raster=[](auto characters){std::vector<s::GlyphMask> out;for(auto cp:characters){(void)cp;out.push_back({0,-2,2,2,{0,128,255,64}});}return out;};
    const auto catalogue=s::compile_external(document,raster);CHECK(catalogue->needs_audio_scene() && catalogue->find_audio(9763,1002) && !catalogue->find_audio(9763,1003));
    CHECK(catalogue->find_audio(8365,2001) && catalogue->find((1008u<<16)|166));
    const auto& cue=catalogue->find((1002u<<16)|9763)->cues.front();CHECK(cue.end_ms==700 && std::count(cue.glyphs.begin(),cue.glyphs.end(),s::line_break)==1);
    const auto quads=s::layout(cue,catalogue->font(),catalogue->glyph_count());CHECK(quads.size()+1==cue.glyphs.size() && quads.front().top<quads.back().top);
    s::Playback playback;playback.catalogue(catalogue);CHECK(playback.play(9763,s::Kind::effect,1,100,false,1002));CHECK(playback.current(799)==&cue && playback.current(800)!=&cue);playback.stop(s::Kind::effect,1);CHECK(!playback.current(801));
    {a::Runtime runtime({}, {}, {},true,catalogue);runtime.load(a::initial_image());auto& p=runtime.process();const auto args=p.allocate_bytes(4096);p.memory.store(args,2,32);p.memory.store(args+4,9763,32);p.memory.store(0x4be4cc,1002,32);
        auto bound=a::find_source_subtitles(p.memory);a::source_subtitle_return(*bound,0x40f400,p.cpu,p.memory,args,500);CHECK(bound->current(500) && !p.executed_instructions);}
    for(const auto& bad:std::vector<std::string>{"{", "{}", "{\n1 2 3\n0 5 X\n}","{\n1 2 3 4 5\n0 5 X\n}","{\n65536 2 3 4\n0 5 X\n}","{\n1 2 3 4\n0 0 X\n}","{\n1 2 3 4\n4294967295 1 X\n}","{\n1 2 3 4\n0 5\n}","{\n1 2 3 4\n0 5 X\n0 1 Y\n}",legacy+legacy,"{\n1 2 3 4\n0 5 "+std::string(513,'x')+"\n}"})rejects([&]{s::parse_external(bytes(bad));});
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.ukrainian.v1\",\"checks\":"<<checks<<",\"guards\":"<<guards<<",\"resource_callback_cases\":"<<resource_cases<<",\"text_policy_cases\":"<<policy_cases<<",\"loose_ukrainian_alias_cases\":"<<loose_aliases<<",\"main_ic_separate\":true,\"matched_font_and_names\":true,\"localized_prompts\":true,\"source_code_immutable\":true,\"legacy_subtitle_audio_binding\":true,\"legacy_newlines_and_overlaps\":true,\"source_executable_opened\":false,\"original_resource_files_opened\":false,\"game_launched\":false,\"screen_access\":false,\"complete_gameplay_verified\":false,\"passed\":true}\n";CHECK(report);
    std::cout<<"PASS shared Ukrainian resources, source text policy, names/prompts and selected Todokanai subtitles.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
