// Data and constructed-memory checks only; never enter game startup.
#include "../recompiled/special/runtime.hpp"
#include "../recompiled/special/images.hpp"
#include "performance_profile.hpp"
#include "../platform/compat/resource_mount.hpp"
#include "../platform/special_subtitle_files.hpp"
#include "../engine/special_subtitles.hpp"
#include <winrecomp/hash.hpp>
#include <winrecomp/resources.hpp>
#include <winrecomp/win32_state.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
namespace sc=wa2::special;namespace r=wa2::resource;namespace s=wa2::subtitles;
unsigned checks{},guards{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("check failed: " #x);}while(false)
template<class F>void rejects(F f){bool bad{};try{f();}catch(const std::exception&){bad=true;}CHECK(bad);++guards;}
void word(std::vector<std::uint8_t>& out,std::uint32_t v){for(unsigned n=0;n<4;++n)out.push_back(std::uint8_t(v>>(8*n)));}
auto raster(std::span<const char32_t> chars){std::vector<s::GlyphMask> out;for(auto cp:chars)out.push_back({0,-16,8,16,std::vector<std::uint8_t>(128,cp==' '?0:255),10});return out;}
int main(int argc,char** argv){try{
    CHECK(argc==2 || argc==3);const auto chinese=sc::initial_image(),english=sc::localized_image(r::GameLanguage::english);
    CHECK(wr::sha256(english.bytes)=="849fd5e414a0ee2b34cec65aba4981c6101f9a59d57b6299c832ae77080b9736");
    CHECK(english.static_input_identity==wr::sha256(english.bytes));CHECK(chinese.bytes==sc::localized_image(r::GameLanguage::japanese).bytes);
    CHECK(english.entry==chinese.entry && english.base==chinese.base && english.sections.size()==chinese.sections.size());
    for(const auto name:{".text",".data"}){const auto find=[&](const wr::Image& image){return std::find_if(image.sections.begin(),image.sections.end(),[&](const auto& section){return section.name==name;});};const auto a=find(chinese),b=find(english);CHECK(a!=chinese.sections.end() && b!=english.sections.end() && a->raw_size==b->raw_size && a->rva==b->rva);CHECK(std::equal(chinese.bytes.begin()+a->raw_offset,chinese.bytes.begin()+a->raw_offset+a->raw_size,english.bytes.begin()+b->raw_offset));}
    const auto parsed=wr::Image::parse(english.bytes);CHECK(parsed.size==english.size && parsed.directories[2].size==english.directories[2].size);
    const auto resources=wr::resource_directory(english);CHECK(!resources.empty());for(const auto& item:resources)CHECK(wr::resource_bytes(english,item).size()==item.size);
    const auto patch_name=english.offset(0x4a13fc,7);CHECK(patch_name);CHECK(std::equal(english.bytes.begin()+*patch_name,english.bytes.begin()+*patch_name+7,reinterpret_cast<const std::uint8_t*>("en.pak\0")));
    for(bool linear:{false,true}){
        wr::ProcessOptions options;options.data_root=std::filesystem::current_path()/"build";options.prefer_linear_memory=linear;
        sc::Runtime runtime(options,{}, {},true,false,{},true,r::GameLanguage::english);auto& p=runtime.process();
        std::vector<std::uint8_t> pak(63);std::copy_n("KCAP",4,pak.begin());pak[12]=1;std::copy_n("panel.tga",9,pak.begin()+20);pak[52]=60;pak[56]=3;pak[60]=1;pak[61]=2;pak[62]=3;
        auto mount=std::make_shared<wa2::platform::compat::ResourceMount>(std::vector<wr::ReadOnlyFileInfo>{{"fixture.pak",pak.size(),1}},[&](std::string_view name){CHECK(name=="fixture.pak");auto* f=std::tmpfile();CHECK(f);CHECK(std::fwrite(pak.data(),1,pak.size(),f)==pak.size());std::rewind(f);return f;});wa2::platform::compat::install_resource_mount(p,mount);
        p.load(english);CHECK(p.input_sha256==english.static_input_identity);
        const auto object=p.allocate_bytes(0x22f8);p.cpu.r[wr::ECX]=object;CHECK(p.callback(0x458630,{})==0);CHECK(p.state().reports.at("native_vorbis")().find("\"constructors\":1")!=std::string::npos);
        const auto profile=sc::image_resource_profile();const auto path=p.put_string("fixture.pak"),name=p.put_string("panel.tga"),output=p.allocate_bytes(4);
        CHECK(p.callback(profile.image_check,std::array<wr::U32,2>{path,name},false)==1);
        const auto heap=p.callback(p.resolve("kernel32.dll","GetProcessHeap"),{});p.memory.store(0xbc3b0c,heap,32);
        CHECK(p.callback(profile.image_load,std::array<wr::U32,3>{path,name,output},false)==3);const auto pointer=p.memory.load(output,32);CHECK(p.memory.load(pointer,8)==1 && p.memory.load(pointer+2,8)==3);
        const auto report=p.state().reports.at("native_image_resources")();CHECK(report.find("\"image_checks\":1")!=report.npos && report.find("\"image_loads\":1")!=report.npos);CHECK(p.executed_instructions==0);
    }
    std::vector<std::uint8_t> text;word(text,0x31554353);word(text,2);word(text,1);word(text,80u<<16);word(text,1);word(text,10);word(text,20);word(text,3);text.insert(text.end(),{'A','\n','B'});
    auto catalogue=s::compile_special_text(text,r::GameLanguage::english,raster);CHECK(catalogue->tracks().size()==1 && catalogue->find(80u<<16)->cues[0].glyphs[1]==s::line_break && !catalogue->needs_audio_scene());
    CHECK(s::special_subtitle_name(r::GameLanguage::chinese)=="subtext.dat" && s::special_subtitle_name(r::GameLanguage::english)=="subtext_en.dat" && s::special_subtitle_name(r::GameLanguage::japanese)=="subtext_ja.dat");
    rejects([&]{s::compile_special_text(text,r::GameLanguage::japanese,raster);});rejects([&]{s::compile_special_text(text,r::GameLanguage::chinese,raster);});rejects([&]{s::special_subtitle_name(r::GameLanguage::ukrainian);});
    for(unsigned n:{0u,4u,12u,31u})rejects([&]{s::compile_special_text(std::span(text).first(n),r::GameLanguage::english,raster);});
    {auto bad=text;bad.push_back(0);rejects([&]{s::compile_special_text(bad,r::GameLanguage::english,raster);});bad=text;bad.back()=0xff;rejects([&]{s::compile_special_text(bad,r::GameLanguage::english,raster);});bad=text;bad[16]=255;rejects([&]{s::compile_special_text(bad,r::GameLanguage::english,raster);});}
    unsigned real_tracks{},real_cues{};if(argc==3){const auto real=wa2::platform::special_subtitle_files(argv[2],r::GameLanguage::english,raster);real_tracks=unsigned(real->tracks().size());for(const auto& track:real->tracks())real_cues+=unsigned(track.cues.size());CHECK(real_tracks==24 && real_cues==2972);}
    std::ofstream out(argv[1]);out<<"{\"passed\":true,\"checks\":"<<checks<<",\"guards\":"<<guards<<",\"english_image_sha256_verified\":true,\"shared_text_and_data\":true,\"english_native_codec_and_resources\":true,\"automatic_subtitle_language_guards\":true,\"external_tracks\":"<<real_tracks<<",\"external_cues\":"<<real_cues<<",\"game_launched\":false,\"screen_access\":false,\"device_gameplay_verified\":false}\n";CHECK(out);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
