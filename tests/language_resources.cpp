#include "../engine/language_resources.hpp"
#include "../platform/compat/language_resources.hpp"
#include "../platform/compat/resource_mount.hpp"
#include "../platform/compat/system.hpp"
#include "../recompiled/process.hpp"
#include "../port/native_resources.hpp"
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <tuple>

#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace r=wa2::resource;
namespace pc=wa2::platform::compat;
unsigned checks{},guards{},aliases{},argument_cases{},english_native_cases{};
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
    packs["en.pak"]=pak({"bak","same.tga","fnt","same.tga","grp","same.tga","script","1001.bnr","1001.txt"},0x31);
    for(unsigned n:{0u,2u,4u,6u}){
        const auto at=16+n*44;std::fill_n(packs["en.pak"].begin()+at,4,0xcc);std::fill_n(packs["en.pak"].begin()+at+40,4,0);
    }
    const auto en_payload=16+9*44;
    packs["en.pak"][en_payload+1]=0x61;packs["en.pak"][en_payload+3]=0x62;packs["en.pak"][en_payload+5]=0x63;
    {auto& packed=packs["en.pak"];const auto offset=packed.size();const auto at=16+8*44;
        packed.insert(packed.end(),{10,0,0,0,1,0,0,0,1,0x31});
        const auto word=[&](std::size_t where,std::uint32_t value){for(unsigned n=0;n<4;++n)packed[where+n]=std::uint8_t(value>>(n*8));};
        word(at,1);word(at+36,std::uint32_t(offset));word(at+40,10);}
    packs["fon_en.pak"]=pak(font_names,0x32);
    packs["bak.pak"]=pak({"same.tga","base-only.tga"},0x41);packs["grp.pak"]=pak({"same.tga","base-only.tga"},0x42);
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
    auto english=process();pc::install_resource_mount(*english,mount);pc::select_language_resources(*english,{r::GameLanguage::english,true});
    for(const auto* name:{"ck-gal.pak","script.pak","IC/ck-gal.pak"}){CHECK(read(*english,name,"1001.txt")==std::vector<std::uint8_t>{0x31});++aliases;}
    for(const auto* name:{"fon.pak","fnt.pak","IC/fon.pak"}){CHECK(read(*english,name,font_names[0])==std::vector<std::uint8_t>{0x32});++aliases;}
    const auto overlay=[&](std::string_view path,std::string_view name){return pc::language_resource_member(*english,root/std::filesystem::path(path),name);};
    CHECK(overlay("ck-gal.pak","1001.txt")->archive->load(overlay("ck-gal.pak","1001.txt")->member)==std::vector<std::uint8_t>{0x31});
    for(const auto* name:{"ck-gal.pak","script.pak","en.pak"}){
        const auto fallback=overlay(name,"Global.vrb");CHECK(fallback && fallback->archive && fallback->archive->load(fallback->member)==std::vector<std::uint8_t>{0x12});
    }
    CHECK(overlay("bak.pak","same.tga")->archive->load(overlay("bak.pak","same.tga")->member)==std::vector<std::uint8_t>{0x61});
    CHECK(overlay("grp.pak","same.tga")->archive->load(overlay("grp.pak","same.tga")->member)==std::vector<std::uint8_t>{0x63});
    CHECK(!overlay("bak.pak","base-only.tga") && !overlay("fon.pak",font_names[0]) && !overlay("patch.pak","1001.txt"));
    CHECK(overlay("ck-gal.pak","unknown")->member==r::Archive::missing);
    CHECK(!pc::language_resource_member(*selected,root/"script.pak","1001.txt"));
    CHECK(!pc::language_resource_member(*english,root.parent_path()/"elsewhere/script.pak","1001.txt"));
    // Exercise the actual shared resource callbacks in owned memory, with an
    // executor that rejects every attempted static game instruction.
    for(bool linear:{false,true}){
        wr::ProcessOptions options;options.data_root=root;options.prefer_linear_memory=linear;
        wr::Process q(wr::FunctionExecutor{[](wr::Cpu&,wr::Memory&){throw std::runtime_error("English resource checks must not execute game instructions");return false;}},options);
        wr::install_wa2_native_resources(q);pc::install_resource_mount(q,mount);pc::select_language_resources(q,{r::GameLanguage::english,false});
        wr::U32 target{};q.replace_api("kernel32.dll","HeapAlloc",3,[&](auto&){return target;});q.load(wa2::aot::initial_image());
        const auto output=q.allocate_bytes(4096);target=q.allocate_bytes(4096);
        const std::array<std::tuple<std::string,std::string,unsigned>,7> cases{{
            {"ck-gal.pak","1001.txt",0x31},{"script.pak","Global.vrb",0x12},{"fon.pak",font_names[0],0x32},
            {"bak.pak","same.tga",0x61},{"grp.pak","same.tga",0x63},{"bak.pak","base-only.tga",0x41},{"patch.pak","1001.txt",0x51}}};
        for(const auto& [file,name,expected]:cases){
            const auto path=q.put_string(file),member=q.put_string(name);
            CHECK(q.callback(0x437220,std::array<wr::U32,2>{path,member},false)!=0);++english_native_cases;
            CHECK(q.callback(0x437310,std::array<wr::U32,3>{path,member,output},false)==1);
            CHECK(q.memory.load(output,32)==target && q.memory.load(target,8)==expected);++english_native_cases;
        }
        CHECK(!q.executed_instructions);
    }
    {auto files=entries;std::erase_if(files,[](const auto& file){return file.name=="script.pak";});auto q=process();auto invalid=std::make_shared<pc::ResourceMount>(files,[&](std::string_view name){return std::fopen((root/std::filesystem::path(name)).string().c_str(),"rb");});pc::install_resource_mount(*q,invalid);rejects([&]{pc::select_language_resources(*q,{r::GameLanguage::english,true});},"script.pak");CHECK(q->state().read_only_files==invalid);}
    for(const auto language:{r::GameLanguage::chinese,r::GameLanguage::japanese,r::GameLanguage::english}){
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
    for(const auto* bad:{"","en-GB","JA","ja","zh","script.pak","../ja-JP"})rejects([&]{r::parse_language(bad);});
    rejects([&]{r::validate_language_resources({static_cast<r::GameLanguage>(99),true});});
    CHECK(r::language_profile(r::GameLanguage::chinese).script=="ck-gal.pak" && r::language_profile(r::GameLanguage::chinese).font=="fon.pak");
    CHECK(r::language_profile(r::GameLanguage::japanese).script=="script.pak" && r::language_profile(r::GameLanguage::japanese).font=="fnt.pak");
    CHECK(r::language_profile(r::GameLanguage::english).script=="en.pak" && r::language_profile(r::GameLanguage::english).font=="fon_en.pak");
    CHECK(r::parse_language("en-US")==r::GameLanguage::english);
    for(bool caption:{false,true})for(const auto language:{r::GameLanguage::chinese,r::GameLanguage::japanese,r::GameLanguage::english}){
        r::LanguageResources options;std::vector<std::string_view> args{caption?"--chinese-subtitles":"--no-chinese-subtitles","--language",r::language_profile(language).tag};
        for(std::size_t n=0;n<args.size();++n)CHECK(r::language_launch_option(args,n,options));CHECK(options.chinese_subtitles==caption && options.language==language);++argument_cases;
    }
    for(const auto* flag:{"--script-pak","--font-pak","--language"})rejects([&]{std::array<std::string_view,1> args{flag};std::size_t n=0;r::LanguageResources value;r::language_launch_option(args,n,value);});
    {std::array<std::string_view,1> args{"--unknown"};std::size_t n=0;r::LanguageResources value;CHECK(!r::language_launch_option(args,n,value) && n==0);}
    {std::array<std::string_view,0> args{};std::size_t n=0;r::LanguageResources value;CHECK(!r::language_launch_option(args,n,value) && n==0);}
    {std::array<std::string_view,2> args{"--language","unknown"};std::size_t n=0;r::LanguageResources value;rejects([&]{r::language_launch_option(args,n,value);});CHECK(value.language==r::GameLanguage::chinese);}
    CHECK(selected->state().reports.at("language_resources")().find("\"chinese_subtitles\":false")!=std::string::npos);
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.language-resources.v3\",\"checks\":"<<checks<<",\"guards\":"<<guards<<",\"archive_aliases\":"<<aliases<<",\"shared_argument_combinations\":"<<argument_cases<<",\"english_native_resource_cases\":"<<english_native_cases<<",\"english_grouped_override_and_base_script_fallback\":true,\"default_chinese_pair\":true,\"automatic_matched_pairs\":true,\"root_ic_and_file_apis_checked\":true,\"patch_priority_preserved\":true,\"invalid_selection_not_published\":true,\"source_executable_opened\":false,\"original_resource_files_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(report);
    std::cout<<"PASS language profiles: matching script/font pairs, aliases, separate file streams and validation.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
