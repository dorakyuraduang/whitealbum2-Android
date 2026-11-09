#include "../engine/language_resources.hpp"
#include "../platform/compat/language_resources.hpp"
#include "../platform/compat/resource_mount.hpp"
#include "../platform/compat/system.hpp"
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>

#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace r=wa2::resource;
namespace pc=wa2::platform::compat;
unsigned checks{},guards{},aliases{},argument_cases{};
template<class F>void rejects(F f,std::string_view name={}){
    bool failed=false;try{f();}catch(const std::exception& error){failed=name.empty() || std::string_view(error.what()).find(name)!=std::string_view::npos;}CHECK(failed);++guards;
}
std::vector<std::uint8_t> pak(const std::vector<std::string>& names,std::uint8_t marker){
    const auto first=16+names.size()*44;std::vector<std::uint8_t> bytes(first+names.size(),marker);
    std::fill_n(bytes.begin(),first,0);std::copy_n("KCAP",4,bytes.begin());
    const auto word=[&](std::size_t at,std::uint32_t value){for(unsigned n=0;n<4;++n)bytes[at+n]=std::uint8_t(value>>(n*8));};word(12,std::uint32_t(names.size()));
    for(std::size_t n=0;n<names.size();++n){CHECK(names[n].size()<=24);const auto at=16+n*44;std::copy(names[n].begin(),names[n].end(),bytes.begin()+at+4);word(at+36,std::uint32_t(first+n));word(at+40,1);}
    return bytes;
}
wr::Image image(){wr::Image value;value.base=0x400000;value.entry=0x401000;value.size=8192;value.headers_size=4096;value.bytes.resize(8192);value.sections={{".text",4096,4096,4096,4096,0x60000020}};return value;}
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto root=std::filesystem::path(argv[1]).parent_path()/("language-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(std::filesystem::create_directory(root));
    const std::vector<std::string> font_names{"\x96\x7b\x91\xcc" "80.tga","\x91\xdc\x89\x65" "80.tga","14pt\x96\x7b\x91\xcc.tga","14pt\x91\xdc.tga"};
    const std::vector<std::string> script_names{"1001.bnr","1001.txt","Global.vrb"};
    std::map<std::string,std::vector<std::uint8_t>> packs{
        {"ck-gal.pak",pak(script_names,0x11)},{"script.pak",pak(script_names,0x12)},
        {"fon.pak",pak(font_names,0x21)},{"fnt.pak",pak(font_names,0x22)},
        {"ic/ck-gal.pak",pak(script_names,0x13)},{"ic/fon.pak",pak(font_names,0x23)},
        {"patch.pak",pak(script_names,0x51)},{"bad.pak",{1,2,3,4,5,6,7,8}}};
    std::vector<wr::ReadOnlyFileInfo> entries;unsigned opens{};
    for(const auto& [name,bytes]:packs){
        const auto path=root/std::filesystem::path(name);if(path.has_parent_path())std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));file.close();CHECK(file);
        entries.push_back({name,bytes.size(),100u+std::uint64_t(bytes.back()),0x21});
    }
    const auto mount=std::make_shared<pc::ResourceMount>(entries,[&](std::string_view name){CHECK(packs.contains(std::string(name)));++opens;return std::fopen((root/std::filesystem::path(name)).string().c_str(),"rb");});
    const auto process=[&]{wr::ProcessOptions options;options.data_root=root;return std::make_unique<wr::Process>(wr::FunctionExecutor{[](wr::Cpu&,wr::Memory&){throw std::runtime_error("Language fixtures must not execute game code");return false;}},options);};
    const auto read=[&](wr::Process& p,std::string_view logical,std::string_view member){auto a=r::Archive::open(pc::open_process_resource(p,root/std::filesystem::path(logical)));return a.load(a.find_first(member));};
    auto base=process();pc::install_resource_mount(*base,mount);pc::select_language_resources(*base,{});CHECK(opens==2);
    CHECK(read(*base,"ck-gal.pak","1001.txt")==std::vector<std::uint8_t>{0x11});CHECK(read(*base,"script.pak","1001.txt")==std::vector<std::uint8_t>{0x11});
    CHECK(read(*base,"fnt.pak",font_names[0])==std::vector<std::uint8_t>{0x21});
    auto selected=process();pc::install_system(*selected);pc::install_resource_mount(*selected,mount);
    r::LanguageResources choice{r::GameLanguage::japanese,false};pc::select_language_resources(*selected,choice);
    for(const auto* name:{"ck-gal.pak","script.pak","IC/CK-GAL.PAK"}){CHECK(read(*selected,name,"1001.txt")==std::vector<std::uint8_t>{0x12});++aliases;}
    for(const auto* name:{"fon.pak","fnt.pak","IC/fon.pak"}){CHECK(read(*selected,name,font_names[0])==std::vector<std::uint8_t>{0x22});++aliases;}
    CHECK(!selected->state().read_only_files->info("IC/script.pak") && !selected->state().read_only_files->info("IC/fnt.pak"));
    CHECK(read(*selected,"patch.pak","1001.txt")==std::vector<std::uint8_t>{0x51});CHECK(read(*base,"fon.pak",font_names[0])==std::vector<std::uint8_t>{0x21});
    CHECK(selected->state().read_only_files->info("CK-GAL.PAK")->modified_100ns==mount->info("script.pak")->modified_100ns);
    for(const auto* directory:{"","IC"}){
        auto listed=selected->state().read_only_files->list(directory);std::vector<std::string> names;
        for(const auto& file:listed)if(!(file.attributes&16)){auto name=file.name;for(auto& c:name)if(c>='A' && c<='Z')c=char(c+32);CHECK(std::find(names.begin(),names.end(),name)==names.end());names.push_back(name);const auto logical=(std::string(directory).empty()?"":std::string(directory)+"/")+file.name;CHECK(file.size==selected->state().read_only_files->info(logical)->size);}
    }
    selected->load(image());auto& p=*selected;const auto work=p.allocate_bytes(4096);
    const auto call=[&](const char* name,std::initializer_list<wr::U32> args){return p.callback(p.resolve("kernel32.dll",name),{args.begin(),args.size()});};
    const auto logical=p.put_string("script.pak");const auto a=call("CreateFileA",{logical,0x80000000,1,0,3,0x80,0}),b=call("CreateFileA",{logical,0x80000000,1,0,3,0x80,0});CHECK(a!=0xffffffff && b!=0xffffffff && a!=b);
    CHECK(call("GetFileSize",{a,0})==packs.at("script.pak").size());CHECK(call("ReadFile",{a,work,4,work+8,0}) && p.memory.load(work,32)==0x5041434b);
    CHECK(call("ReadFile",{a,work,4,work+8,0}) && p.memory.load(work,32)==0);CHECK(call("ReadFile",{b,work,4,work+8,0}) && p.memory.load(work,32)==0x5041434b);
    CHECK(call("CreateFileA",{logical,0x40000000,1,0,2,0x80,0})==0xffffffff && p.last_error==5);
    CHECK(call("CloseHandle",{a}) && call("CloseHandle",{b}));rejects([&]{pc::select_language_resources(p,choice);});
    for(const auto language:{r::GameLanguage::chinese,r::GameLanguage::japanese}){
        const auto profile=r::language_profile(language);
        for(const auto target:{profile.script,profile.font})for(int failure=0;failure<3;++failure){
            auto files=entries;if(!failure)std::erase_if(files,[&](const auto& entry){return entry.name==target;});
            auto invalid=std::make_shared<pc::ResourceMount>(files,[&](std::string_view name){
                const auto physical=name==target?(failure==1?"bad.pak":target==profile.script?profile.font:profile.script):name;
                return std::fopen((root/std::filesystem::path(physical)).string().c_str(),"rb");});
            auto q=process();pc::install_resource_mount(*q,invalid);rejects([&]{pc::select_language_resources(*q,{language,true});},target);CHECK(q->state().read_only_files==invalid && !q->state().reports.contains("language_resources"));
        }
    }
    {auto q=process();rejects([&]{pc::select_language_resources(*q,{});},"mounted");}
    for(const auto* bad:{"","en-US","JA","ja","zh","script.pak","../ja-JP"})rejects([&]{r::parse_language(bad);});
    rejects([&]{r::validate_language_resources({static_cast<r::GameLanguage>(99),true});});
    CHECK(r::language_profile(r::GameLanguage::chinese).script=="ck-gal.pak" && r::language_profile(r::GameLanguage::chinese).font=="fon.pak");
    CHECK(r::language_profile(r::GameLanguage::japanese).script=="script.pak" && r::language_profile(r::GameLanguage::japanese).font=="fnt.pak");
    for(bool caption:{false,true})for(const auto language:{r::GameLanguage::chinese,r::GameLanguage::japanese}){
        r::LanguageResources options;std::vector<std::string_view> args{caption?"--chinese-subtitles":"--no-chinese-subtitles","--language",r::language_profile(language).tag};
        for(std::size_t n=0;n<args.size();++n)CHECK(r::language_launch_option(args,n,options));CHECK(options.chinese_subtitles==caption && options.language==language);++argument_cases;
    }
    for(const auto* flag:{"--script-pak","--font-pak","--language"})rejects([&]{std::array<std::string_view,1> args{flag};std::size_t n=0;r::LanguageResources value;r::language_launch_option(args,n,value);});
    {std::array<std::string_view,1> args{"--unknown"};std::size_t n=0;r::LanguageResources value;CHECK(!r::language_launch_option(args,n,value) && n==0);}
    {std::array<std::string_view,0> args{};std::size_t n=0;r::LanguageResources value;CHECK(!r::language_launch_option(args,n,value) && n==0);}
    {std::array<std::string_view,2> args{"--language","unknown"};std::size_t n=0;r::LanguageResources value;rejects([&]{r::language_launch_option(args,n,value);});CHECK(value.language==r::GameLanguage::chinese);}
    CHECK(selected->state().reports.at("language_resources")().find("\"chinese_subtitles\":false")!=std::string::npos);
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.language-resources.v2\",\"checks\":"<<checks<<",\"guards\":"<<guards<<",\"archive_aliases\":"<<aliases<<",\"shared_argument_combinations\":"<<argument_cases<<",\"default_chinese_pair\":true,\"automatic_matched_pairs\":true,\"root_ic_and_file_apis_checked\":true,\"patch_priority_preserved\":true,\"invalid_selection_not_published\":true,\"source_executable_opened\":false,\"original_resource_files_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(report);
    std::cout<<"PASS language profiles: matching script/font pairs, aliases, separate file streams and validation.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
