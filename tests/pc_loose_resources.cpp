// Synthetic PC patch layout and certified static I/O helpers. No game/UI run.
#include "../recompiled/process.hpp"
#include "../recompiled/language_names.hpp"
#include "../recompiled/language_prompts.hpp"
#include "../platform/compat/language_resources.hpp"
#include "../platform/compat/resource_mount.hpp"
#include "../platform/compat/codepage.hpp"
#include <winrecomp/win32_state.hpp>
#include <winrecomp/path_name.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
namespace a=wa2::aot;namespace c=wa2::platform::compat;namespace r=wa2::resource;
using Bytes=std::vector<std::uint8_t>;using wr::U32;
unsigned cases{},calls{},guards{},profiles{};
#define CHECK(x) do{++cases;if(!(x))throw std::runtime_error(std::string("PC resource check at ")+std::to_string(__LINE__)+": "+#x);}while(false)
void put(const std::filesystem::path& path,const Bytes& bytes){std::filesystem::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());file.close();CHECK(file);}
Bytes pak(const std::vector<std::pair<std::string,unsigned>>& entries){Bytes bytes(16+entries.size()*45);std::copy_n("KCAP",4,bytes.begin());
    const auto word=[&](unsigned at,U32 value){for(unsigned n=0;n<4;++n)bytes[at+n]=std::uint8_t(value>>(n*8));};word(12,U32(entries.size()));
    for(unsigned n=0;n<entries.size();++n){const auto& [name,marker]=entries[n];CHECK(name.size()<=24);std::copy(name.begin(),name.end(),bytes.begin()+16+n*44+4);word(16+n*44+36,unsigned(16+entries.size()*44+n));word(16+n*44+40,marker?1:0);bytes[16+entries.size()*44+n]=std::uint8_t(marker);}return bytes;}
std::filesystem::path sjis(std::string_view text){const auto wide=c::decode_text(932,{reinterpret_cast<const std::uint8_t*>(text.data()),text.size()},true);const auto utf8=c::encode_text(65001,wide,{},true);return wr::utf8_file_path({reinterpret_cast<const char*>(utf8.bytes.data()),utf8.bytes.size()});}
void exercise(const std::filesystem::path& root,r::GameLanguage language,bool linear){
    const auto folder=root/(std::to_string(unsigned(language))+(linear?"-linear":"-paged"));std::filesystem::create_directory(folder);const auto data=folder/"selected",private_root=folder/"private";std::filesystem::create_directory(data);std::filesystem::create_directory(private_root);
    const std::array<std::string,4> fonts{"\x96\x7b\x91\xcc" "80.tga","\x91\xdc\x89\x65" "80.tga","14pt\x96\x7b\x91\xcc.tga","14pt\x91\xdc.tga"};
    std::vector<std::pair<std::string,unsigned>> entries{{"bak",0},{"same.bin",0x61},{"grp",0},{"same.bin",0x62},{"script",0},{"1001.bnr",0x30},{"1001.txt",0x31},{"1002.bnr",0x32},{"1002.txt",0x33},{"fnt",0}};
    for(unsigned n=0;n<fonts.size();++n)entries.emplace_back(fonts[n],0x70+n);
    put(data/"en.pak",pak(entries));put(data/"script.pak",pak({{"1001.bnr",0x40},{"1001.txt",0x41},{"Global.vrb",0x45}}));put(data/"IC"/"script.pak",pak({{"Global.vrb",0x46}}));put(data/"IC"/"ck-gal.pak",pak({{"Global.vrb",0x47}}));put(data/"grp.pak",pak({{"same.bin",0x48}}));
    put(data/"SCRIPT"/"1001.bnr",{0xa0});put(data/"SCRIPT"/"1001.txt",{0xa1});put(data/"IC"/"script"/"1001.txt",{0xa2});
    put(data/"FNT"/sjis(fonts[0]),{0xa3});put(data/"FNT"/sjis(fonts[1]),{0xa4});put(data/"IC"/"fnt"/sjis(fonts[0]),{0xa5});
    const auto profile=r::language_profile(language);const auto legacy_script=std::string(profile.script.substr(0,profile.script.size()-4)),legacy_font=std::string(profile.font.substr(0,profile.font.size()-4));
    put(data/legacy_script/"1001.txt",{0xb1});put(data/legacy_script/"legacy-only.bin",{0xb2});put(data/legacy_font/sjis(fonts[0]),{0xb3});put(data/legacy_font/"legacy-only.bin",{0xb4});
    put(private_root/"script"/"1002.txt",{0xee});
    wr::ProcessOptions options;options.data_root=private_root;options.allow_file_write=true;options.prefer_linear_memory=linear;a::Runtime runtime(options);auto& p=runtime.process();auto original=c::local_resource_mount(data);c::install_resource_mount(p,original);
    CHECK(!original->info(profile.font));CHECK(!original->info("fnt.pak"));c::select_language_resources(p,{language,true});++profiles;
    auto view=p.state().read_only_files;CHECK(view->info("fon.pak") && view->info("fnt.pak"));CHECK(p.state().reports.at("language_resources")().find("\"font_from_patch_group\":true")!=std::string::npos);
    CHECK(!view->info("script/missing.bin") && view->owns_path("script/missing.bin"));CHECK(!view->info("fnt/missing.bin") && view->owns_path("fnt/missing.bin"));guards+=2;
    const auto list=view->list("fon");std::set<std::string> names;for(const auto& item:list)CHECK(names.insert(item.name).second);CHECK(names.contains("legacy-only.bin"));
    runtime.load();a::configure_language_names(p,language);a::configure_language_prompts(p,language);CHECK(p.state().reports.contains("ukrainian_text")== (language==r::GameLanguage::ukrainian));const auto output=p.allocate_bytes(4);
    const auto load=[&](const char* group,const std::string& name,unsigned expected){p.memory.store(output,0,32);CHECK(p.callback(0x437820,std::array<U32,3>{p.put_string(group),p.put_string(name),output},false)==1);const auto pointer=p.memory.load(output,32);CHECK(pointer && p.memory.load(pointer,8)==expected);CHECK(p.callback(p.resolve("kernel32.dll","HeapFree"),std::array<U32,3>{p.callback(p.resolve("kernel32.dll","GetProcessHeap"),std::array<U32,0>{}),0,pointer}));++calls;};
    load("ck-gal","1001.txt",0xa1);load("script","1001.txt",0xa1);load("IC/ck-gal","1001.txt",0xa2);
    load("fon",fonts[0],0xa3);load("fnt",fonts[1],0xa4);load("IC/fon",fonts[0],0xa5);
    load("ck-gal","legacy-only.bin",0xb2);load("fon","legacy-only.bin",0xb4);
    load("ck-gal","1002.txt",0x33);load("fon",fonts[2],0x72);load("fon",fonts[3],0x73);
    load("ck-gal","Global.vrb",0x45);load("IC/ck-gal","Global.vrb",0x46);
    const auto from=p.put_string("fnt/never.bin");CHECK(p.callback(p.resolve("kernel32.dll","CreateFileA"),std::array<U32,7>{from,0x40000000,7,0,2,128,0})==0xffffffff && p.last_error==5);++guards;
    CHECK(!p.gui() && !p.directdraw());
    for(unsigned invalid=0;invalid<2;++invalid){const auto bad=folder/("invalid-"+std::to_string(invalid));auto broken=entries;if(invalid)broken.pop_back();else std::erase_if(broken,[](const auto& item){return item.first=="fnt";});put(bad/"en.pak",pak(broken));put(bad/"script.pak",pak({{"1001.bnr",1},{"1001.txt",2}}));
        auto mount=c::local_resource_mount(bad);a::Runtime rejected(options);auto& q=rejected.process();c::install_resource_mount(q,mount);bool failed{};try{c::select_language_resources(q,{language,true});}catch(const std::exception& error){failed=std::string(error.what()).find(invalid?"font atlases":"fnt group")!=std::string::npos;}
        CHECK(failed && q.state().read_only_files==mount && !q.state().reports.contains("language_resources"));++guards;
    }
}
int main(int argc,char** argv){try{CHECK(argc==2);const auto root=std::filesystem::path(argv[1]).parent_path()/("pc-loose-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(root);
    for(auto language:{r::GameLanguage::english,r::GameLanguage::ukrainian})for(bool linear:{false,true})exercise(root,language,linear);
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.pc-loose-resources.v1\",\"cases\":"<<cases<<",\"static_reader_calls\":"<<calls<<",\"profiles\":"<<profiles<<",\"guards\":"<<guards<<",\"pc_script_and_font_priority\":true,\"main_ic_separate\":true,\"partial_font_patch_falls_back_to_en_group\":true,\"legacy_loose_folders_preserved\":true,\"prepared_sidecars_not_required\":true,\"private_resource_fallback_forbidden\":true,\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(report);std::cout<<"PASS PC loose script/font priority, grouped font fallback, legacy folders and both language selections.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
