#include "../recompiled/language_prompts.hpp"
#include "../recompiled/language_names.hpp"
#include "../recompiled/process.hpp"
#include <winrecomp/win32_state.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <set>

namespace a=wa2::aot;
namespace r=wa2::resource;
unsigned checks{},guards{},cases{},localized{},glyphs{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F action){bool caught=false;try{action();}catch(const std::exception&){caught=true;}CHECK(caught);++guards;}
std::vector<std::uint8_t> bytes(wr::Process& p,wr::U32 at,std::size_t size){std::vector<std::uint8_t> data(size);p.memory.copy_out(at,data);return data;}
std::string_view selected(const a::MenuPrompt& p,r::GameLanguage language){
    switch(language){case r::GameLanguage::chinese:return p.chinese;case r::GameLanguage::japanese:return p.japanese;case r::GameLanguage::english:return p.english;}
    throw std::runtime_error("invalid fixture language");
}
void check_glyphs(wr::Process& p,std::string_view text,bool english){
    for(std::size_t n=0;n<text.size();){
        auto code=unsigned(static_cast<unsigned char>(text[n++]));if(code=='\n')continue;
        CHECK(code>=32);if(english)CHECK(code<128);
        if((code>=0x81 && code<=0x9f) || code>=0xe0){CHECK(n<text.size());code=(code<<8)|static_cast<unsigned char>(text[n++]);}
        else if(code>=0xa1 && code<=0xdf)code|=0xff00;
        // Use the actual immutable source tables, including their ordering.
        for(auto table:{0x4a1420u,0x4a14b8u}){
            unsigned index{};bool found=false;
            for(unsigned row=0;row<64;++row){const auto first=p.memory.load(table+row*4,16),last=p.memory.load(table+row*4+2,16);
                if(first==0xffff || last==0xffff)break;
                if(first<=code && code<=last){index+=code-first;found=true;break;}index+=last-first+1;
            }
            CHECK(found);
            // English's small atlas has only six rows of 80 cells. ASCII
            // prompts must never request Chinese/Japanese glyph positions.
            CHECK(index<(english?480u:7680u));++glyphs;
        }
    }
}
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto image=a::initial_image();const auto prompts=a::language_menu_prompts();CHECK(prompts.size()==7);
    const std::set<wr::U32> required{0x4a2c1c,0x4a2dc4,0x4a2de4,0x4a2e10,0x4a2e44,0x4a2f00,0x4a2f34};
    std::set<wr::U32> addresses;
    for(const auto& prompt:prompts){CHECK(addresses.insert(prompt.address).second);CHECK(prompt.japanese!=prompt.chinese && prompt.english!=prompt.chinese);
        CHECK(prompt.japanese.size()<=prompt.chinese.size() && prompt.english.size()<=prompt.chinese.size());}
    CHECK(addresses==required);
    const auto section=std::find_if(image.sections.begin(),image.sections.end(),[](const auto& s){return s.name==".rdata";});CHECK(section!=image.sections.end());
    const auto base=image.base+section->rva;
    for(bool linear:{false,true}){
        wr::ProcessOptions options;options.prefer_linear_memory=linear;
        for(auto language:{r::GameLanguage::chinese,r::GameLanguage::japanese,r::GameLanguage::english}){
            a::Runtime runtime(options);runtime.load(image);auto& p=runtime.process();
            a::configure_language_names(p,language);
            const auto code=bytes(p,0x401000,654336),data=bytes(p,0x4be000,85504),original=bytes(p,base,section->raw_size);
            const auto registers=p.cpu;const auto epoch=p.memory.code_epoch();auto expected=original;
            for(const auto& prompt:prompts){const auto text=selected(prompt,language);const auto offset=prompt.address-base;
                std::fill_n(expected.begin()+offset,prompt.chinese.size()+1,0);std::copy(text.begin(),text.end(),expected.begin()+offset);}
            a::configure_language_prompts(p,language);
            CHECK(bytes(p,base,expected.size())==expected);
            if(language==r::GameLanguage::chinese)CHECK(expected==original);
            for(const auto& prompt:prompts){const auto text=selected(prompt,language);
                CHECK(p.read_string(prompt.address,prompt.chinese.size()+1)==text);
                CHECK(p.memory.permissions(prompt.address)==wr::Memory::Read);
                if(language!=r::GameLanguage::chinese){check_glyphs(p,text,language==r::GameLanguage::english);++localized;}
            }
            CHECK(bytes(p,0x401000,code.size())==code && bytes(p,0x4be000,data.size())==data && p.memory.code_epoch()==epoch);
            CHECK(p.cpu.r==registers.r && p.cpu.eip==registers.eip && !p.native_function_entries && !p.executed_instructions);
            const auto report=p.state().reports.at("language_prompts")();
            CHECK(report.find(std::string(r::language_profile(language).tag))!=std::string::npos);
            CHECK(report.find("\"changed_prompts\":"+std::to_string(language==r::GameLanguage::chinese?0:7))!=std::string::npos);
            rejects([&]{a::configure_language_prompts(p,r::GameLanguage::english);});CHECK(bytes(p,base,expected.size())==expected);++cases;
        }
        // A bad later literal must reject the whole initialization before any
        // earlier prompt is replaced. Check each of the seven contracts.
        for(const auto& prompt:prompts){
            a::Runtime runtime(options);runtime.load(image);auto& p=runtime.process();
            const std::array<std::uint8_t,1> changed{'!'};p.memory.initialize(prompt.address+4,changed);
            const auto before=bytes(p,base,section->raw_size);
            rejects([&]{a::configure_language_prompts(p,r::GameLanguage::japanese);});
            CHECK(bytes(p,base,before.size())==before && !p.state().reports.contains("language_prompts"));
        }
        for(unsigned fault=0;fault<6;++fault){
            a::Runtime runtime(options);runtime.load(image);auto& p=runtime.process();
            if(fault==0)p.input_sha256="unknown";
            if(fault==1)p.native_function_entries=1;
            if(fault==2)p.executed_instructions=1;
            if(fault==3)p.memory.protect(0x4a2000,4096,wr::Memory::Read|wr::Memory::Write);
            if(fault==4){const std::array<std::uint8_t,1> changed{'!'};p.memory.initialize(prompts.back().address+prompts.back().chinese.size(),changed);}
            const auto before=bytes(p,base,section->raw_size);
            rejects([&]{a::configure_language_prompts(p,fault==5?static_cast<r::GameLanguage>(99):r::GameLanguage::english);});
            CHECK(bytes(p,base,before.size())==before && !p.state().reports.contains("language_prompts"));
        }
        a::Runtime empty(options);rejects([&]{a::configure_language_prompts(empty.process(),r::GameLanguage::english);});
    }
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.language-menu-prompts.v1\",\"checks\":"<<checks<<",\"guards\":"<<guards<<",\"language_memory_cases\":"<<cases<<",\"localized_prompt_cases\":"<<localized<<",\"glyph_table_cases\":"<<glyphs<<",\"prompts_per_language\":7,\"default_chinese_unchanged\":true,\"japanese_source_text\":true,\"english_ascii_prompts\":true,\"adjacent_data_and_code_unchanged\":true,\"read_only_permissions_preserved\":true,\"all_contracts_checked_before_replacement\":true,\"static_function_entries\":0,\"instruction_step_calls\":0,\"source_executable_opened\":false,\"original_resource_files_opened\":false,\"game_launched\":false,\"screen_access\":false,\"complete_gameplay_verified\":false,\"passed\":true}\n";CHECK(out);
    std::cout<<"PASS shared menu prompts: Chinese/Japanese/English, both memory modes, bounded data-only initialization and matching glyphs.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
