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
        {"script-ja.pak",pak({"1001.BNR","1001.TXT","Global.VRB"},0x31)},{"ic/font-ja.pak",pak(font_names,0x41)},
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
    auto base=process();pc::install_resource_mount(*base,mount);pc::select_language_resources(*base,{});CHECK(opens==0 && base->state().read_only_files==mount);
    CHECK(read(*base,"ck-gal.pak","1001.txt")==std::vector<std::uint8_t>{0x11});CHECK(read(*base,"script.pak","1001.txt")==std::vector<std::uint8_t>{0x12});
    auto selected=process();pc::install_system(*selected);pc::install_resource_mount(*selected,mount);
    r::LanguageResources choice{"SCRIPT-JA.PAK","IC/FONT-JA.PAK",false};pc::select_language_resources(*selected,choice);
    for(const auto* name:{"ck-gal.pak","script.pak","IC/CK-GAL.PAK"}){CHECK(read(*selected,name,"1001.txt")==std::vector<std::uint8_t>{0x31});++aliases;}
    for(const auto* name:{"fon.pak","fnt.pak","IC/fon.pak"}){CHECK(read(*selected,name,font_names[0])==std::vector<std::uint8_t>{0x41});++aliases;}
    CHECK(!selected->state().read_only_files->info("IC/script.pak") && !selected->state().read_only_files->info("IC/fnt.pak"));
    CHECK(read(*selected,"patch.pak","1001.txt")==std::vector<std::uint8_t>{0x51});CHECK(read(*base,"fon.pak",font_names[0])==std::vector<std::uint8_t>{0x21});
    CHECK(selected->state().read_only_files->info("CK-GAL.PAK")->modified_100ns==mount->info("script-ja.pak")->modified_100ns);
    for(const auto* directory:{"","IC"}){
        auto listed=selected->state().read_only_files->list(directory);std::vector<std::string> names;
        for(const auto& file:listed)if(!(file.attributes&16)){auto name=file.name;for(auto& c:name)if(c>='A' && c<='Z')c=char(c+32);CHECK(std::find(names.begin(),names.end(),name)==names.end());names.push_back(name);const auto logical=(std::string(directory).empty()?"":std::string(directory)+"/")+file.name;CHECK(file.size==selected->state().read_only_files->info(logical)->size);}
    }
    selected->load(image());auto& p=*selected;const auto work=p.allocate_bytes(4096);
    const auto call=[&](const char* name,std::initializer_list<wr::U32> args){return p.callback(p.resolve("kernel32.dll",name),{args.begin(),args.size()});};
    const auto logical=p.put_string("script.pak");const auto a=call("CreateFileA",{logical,0x80000000,1,0,3,0x80,0}),b=call("CreateFileA",{logical,0x80000000,1,0,3,0x80,0});CHECK(a!=0xffffffff && b!=0xffffffff && a!=b);
    CHECK(call("GetFileSize",{a,0})==packs.at("script-ja.pak").size());CHECK(call("ReadFile",{a,work,4,work+8,0}) && p.memory.load(work,32)==0x5041434b);
    CHECK(call("ReadFile",{a,work,4,work+8,0}) && p.memory.load(work,32)==0);CHECK(call("ReadFile",{b,work,4,work+8,0}) && p.memory.load(work,32)==0x5041434b);
    CHECK(call("CreateFileA",{logical,0x40000000,1,0,2,0x80,0})==0xffffffff && p.last_error==5);
    CHECK(call("CloseHandle",{a}) && call("CloseHandle",{b}));rejects([&]{pc::select_language_resources(p,choice);});
    for(const auto bad:{r::LanguageResources{"missing.pak","",true},r::LanguageResources{"bad.pak","",true},r::LanguageResources{"fon.pak","",true},r::LanguageResources{"","script.pak",true}}){
        auto q=process();pc::install_resource_mount(*q,mount);const auto name=bad.script.empty()?bad.font:bad.script;
        rejects([&]{pc::select_language_resources(*q,bad);},name);CHECK(q->state().read_only_files==mount);
    }
    auto font_only=process();pc::install_resource_mount(*font_only,mount);pc::select_language_resources(*font_only,{"","ic/font-ja.pak",true});CHECK(read(*font_only,"script.pak","1001.txt")==std::vector<std::uint8_t>{0x12});
    auto script_only=process();pc::install_resource_mount(*script_only,mount);pc::select_language_resources(*script_only,{"script-ja.pak","",true});CHECK(read(*script_only,"fnt.pak",font_names[0])==std::vector<std::uint8_t>{0x22});
    for(const auto* bad:{"../bad.pak","other/a.pak","IC/../bad.pak","IC\\font.pak",".hidden.pak","font.ttf","font.otf","bad/name.pak","\xe5\xad\x97.pak","\xe2\x84\xaa.pak"})rejects([&]{r::language_archive_name(bad);});
    CHECK(r::language_archive_name("").empty());CHECK(r::language_archive_name("IC/FONT-JA.PAK")=="ic/font-ja.pak");CHECK(r::language_archive_name(std::string(124,'a')+".pak").size()==128);rejects([&]{r::language_archive_name(std::string(125,'a')+".pak");});
    for(bool caption:{false,true})for(bool script:{false,true})for(bool font:{false,true}){
        r::LanguageResources options;std::vector<std::string_view> args{caption?"--chinese-subtitles":"--no-chinese-subtitles"};if(script){args.push_back("--script-pak");args.push_back("SCRIPT-JA.PAK");}if(font){args.push_back("--font-pak");args.push_back("IC/FONT-JA.PAK");}
        for(std::size_t n=0;n<args.size();++n)CHECK(r::language_launch_option(args,n,options));CHECK(options.chinese_subtitles==caption && options.script==(script?"script-ja.pak":"") && options.font==(font?"ic/font-ja.pak":""));++argument_cases;
    }
    for(const auto* flag:{"--script-pak","--font-pak"})rejects([&]{std::array<std::string_view,1> args{flag};std::size_t n=0;r::LanguageResources value;r::language_launch_option(args,n,value);},flag);
    {std::array<std::string_view,1> args{"--unknown"};std::size_t n=0;r::LanguageResources value;CHECK(!r::language_launch_option(args,n,value) && n==0);}
    CHECK(selected->state().reports.at("language_resources")().find("\"chinese_subtitles\":false")!=std::string::npos);
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.language-resources.v1\",\"checks\":"<<checks<<",\"guards\":"<<guards<<",\"archive_aliases\":"<<aliases<<",\"shared_argument_combinations\":"<<argument_cases<<",\"default_mount_unchanged\":true,\"independent_script_and_font\":true,\"root_ic_and_file_apis_checked\":true,\"patch_priority_preserved\":true,\"invalid_selection_not_published\":true,\"source_executable_opened\":false,\"original_resource_files_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(report);
    std::cout<<"PASS language PAK selection: default preservation, aliases, original file APIs, separate streams and bounded validation.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
