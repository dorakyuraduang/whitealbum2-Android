#include "../engine/language_resources.hpp"
#include "../platform/compat/language_resources.hpp"
#include "../platform/compat/resource_mount.hpp"
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>

namespace r=wa2::resource;
namespace pc=wa2::platform::compat;
unsigned checks{},guards{},aliases{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("check failed: " #x);}while(false)
template<class F>void rejects(F f){bool failed{};try{f();}catch(const std::exception&){failed=true;}CHECK(failed);++guards;}
class ConstructedLoose final:public wr::ReadOnlyFiles {
    const std::map<std::string,std::vector<std::uint8_t>>& files_;
    std::function<std::FILE*(std::string_view)> open_;
public:
    ConstructedLoose(const std::map<std::string,std::vector<std::uint8_t>>& files,std::function<std::FILE*(std::string_view)> open):files_(files),open_(std::move(open)){}
    std::optional<wr::ReadOnlyFileInfo> info(std::string_view path)const override{const auto found=files_.find(std::string(path));if(found==files_.end() || path.ends_with(".pak"))return {};return wr::ReadOnlyFileInfo{std::string(path),found->second.size(),123,0x21};}
    bool owns_path(std::string_view path)const override{return bool(info(path));}
    std::vector<wr::ReadOnlyFileInfo> list(std::string_view)const override{return {};}
    std::FILE* open(std::string_view path)override{return info(path)?open_(path):nullptr;}
};
std::vector<std::uint8_t> pak(std::span<const std::string> names,std::uint8_t marker){
    const auto first=16+names.size()*44;std::vector<std::uint8_t> bytes(first+names.size(),marker);std::fill_n(bytes.begin(),first,0);std::copy_n("KCAP",4,bytes.begin());
    const auto word=[&](std::size_t at,std::uint32_t value){for(unsigned n=0;n<4;++n)bytes[at+n]=std::uint8_t(value>>(8*n));};word(12,std::uint32_t(names.size()));
    for(std::size_t n=0;n<names.size();++n){CHECK(names[n].size()<=24);const auto at=16+n*44;std::copy(names[n].begin(),names[n].end(),bytes.begin()+at+4);word(at+36,std::uint32_t(first+n));word(at+40,1);}
    return bytes;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);
    const std::array<std::string,4> font{"\x96\x7b\x91\xcc" "80.tga","\x91\xdc\x89\x65" "80.tga","14pt\x96\x7b\x91\xcc.tga","14pt\x91\xdc.tga"};
    const std::array<std::string,2> script{"1001.bnr","1001.txt"};const std::array<std::string,1> ui{"logo.tga"};
    std::map<std::string,std::vector<std::uint8_t>> packs{
        {"gcn.pak",pak(ui,0x31)},{"grp.pak",pak(ui,0x32)},
        {"script_cn.pak",pak(script,0x11)},{"script_cn2.pak",pak(script,0x12)},
        {"script.pak",pak(script,0x13)},{"ck-gal.pak",pak(script,0x14)},
        {"fon.pak",pak(font,0x21)},{"fnt.pak",pak(font,0x22)},
        {"patch.pak",pak(ui,0x41)},{"grp/loose.tga",{0x51}},{"script/loose.txt",{0x52}},{"fnt/loose.tga",{0x53}}};
    std::vector<wr::ReadOnlyFileInfo> entries;for(const auto& [name,bytes]:packs)if(name.ends_with(".pak"))entries.push_back({name,bytes.size(),123,0x21});
    unsigned opens{};const auto open=[&](std::string_view name){++opens;const auto found=packs.find(std::string(name));CHECK(found!=packs.end());auto* file=std::tmpfile();CHECK(file);const auto& bytes=found->second;CHECK(std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size());std::rewind(file);return file;};
    const auto mount=std::make_shared<pc::ResourceMount>(entries,open,std::make_shared<ConstructedLoose>(packs,open));
    // Process requires an existing root; every resource open is intercepted by
    // the constructed mount above and can only return our in-memory fixture.
    const auto root=std::filesystem::current_path();
    const auto process=[&]{wr::ProcessOptions options;options.data_root=root;return std::make_unique<wr::Process>(wr::FunctionExecutor{[](wr::Cpu&,wr::Memory&)->bool{throw std::runtime_error("No game instruction may execute");}},options);};
    const auto read=[&](wr::Process& p,std::string_view path,std::string_view member){auto archive=r::Archive::open(pc::open_process_resource(p,root/std::filesystem::path(path)));return archive.load(archive.find_first(member));};
    const auto special=r::ResourceTitle::special_contents;
    auto chinese=process();pc::install_resource_mount(*chinese,mount);const auto before=opens;pc::select_language_resources(*chinese,{},special);
    CHECK(chinese->state().read_only_files==mount && opens==before);
    CHECK(read(*chinese,"script_cn.pak","1001.txt")==std::vector<std::uint8_t>{0x11});
    CHECK(read(*chinese,"script_cn2.pak","1001.txt")==std::vector<std::uint8_t>{0x12});
    CHECK(read(*chinese,"gcn.pak","logo.tga")==std::vector<std::uint8_t>{0x31});
    CHECK(read(*chinese,"fon.pak",font[0])==std::vector<std::uint8_t>{0x21});
    auto japanese=process();pc::install_resource_mount(*japanese,mount);pc::select_language_resources(*japanese,{r::GameLanguage::japanese,false},special);
    for(const auto path:{"gcn.pak","grp.pak"}){CHECK(read(*japanese,path,"logo.tga")==std::vector<std::uint8_t>{0x32});++aliases;}
    for(const auto path:{"script_cn.pak","script_cn2.pak","script.pak"}){CHECK(read(*japanese,path,"1001.txt")==std::vector<std::uint8_t>{0x13});++aliases;}
    for(const auto path:{"fon.pak","fnt.pak"}){CHECK(read(*japanese,path,font[0])==std::vector<std::uint8_t>{0x22});++aliases;}
    CHECK(read(*japanese,"patch.pak","logo.tga")==std::vector<std::uint8_t>{0x41});
    CHECK(!japanese->state().read_only_files->info("IC/script_cn.pak"));
    for(const auto path:{"gcn/loose.tga","script_cn/loose.txt","fon/loose.tga"}){CHECK(japanese->state().read_only_files->info(path));auto* file=japanese->state().read_only_files->open(path);CHECK(file);CHECK(std::fgetc(file)>=0x51);std::fclose(file);++aliases;}
    // The existing Main aliases retain their two supported resource pairs.
    for(const auto language:{r::GameLanguage::chinese,r::GameLanguage::japanese}){
        auto main=process();pc::install_resource_mount(*main,mount);pc::select_language_resources(*main,{language,true});
        CHECK(read(*main,"ck-gal.pak","1001.txt")==std::vector<std::uint8_t>{std::uint8_t(language==r::GameLanguage::chinese?0x14:0x13)});
        CHECK(read(*main,"fon.pak",font[0])==std::vector<std::uint8_t>{std::uint8_t(language==r::GameLanguage::chinese?0x21:0x22)});CHECK(!main->executed_instructions);
    }
    for(const auto missing:{"grp.pak","script.pak","fnt.pak"}){
        auto fewer=entries;std::erase_if(fewer,[&](const auto& item){return item.name==missing;});auto incomplete=std::make_shared<pc::ResourceMount>(fewer,open);auto p=process();pc::install_resource_mount(*p,incomplete);
        rejects([&]{pc::select_language_resources(*p,{r::GameLanguage::japanese,true},special);});CHECK(p->state().read_only_files==incomplete && !p->state().reports.contains("language_resources"));
    }
    for(const auto language:{r::GameLanguage::english,r::GameLanguage::ukrainian,static_cast<r::GameLanguage>(99)})rejects([&]{r::language_profile(language,special);});
    rejects([&]{r::language_profile(r::GameLanguage::chinese,static_cast<r::ResourceTitle>(99));});
    CHECK(!chinese->executed_instructions && !japanese->executed_instructions);
    const auto report=japanese->state().reports.at("language_resources")();CHECK(report.find("\"ui\":\"grp.pak\"")!=report.npos && report.find("\"script\":\"script.pak\"")!=report.npos && report.find("\"font\":\"fnt.pak\"")!=report.npos);
    std::ofstream out(argv[1]);out<<"{\"passed\":true,\"checks\":"<<checks<<",\"guards\":"<<guards<<",\"japanese_alias_cases\":"<<aliases<<",\"chinese_banks_preserved\":true,\"main_pairs_preserved\":true,\"loose_resources_routed\":true,\"patch_priority_preserved\":true,\"missing_pack_atomic_failure\":true,\"original_files_opened\":false,\"game_launched\":false,\"screen_access\":false,\"device_gameplay_verified\":false}\n";CHECK(out);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
