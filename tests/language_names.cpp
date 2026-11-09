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
unsigned checks{},guards{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F action){bool caught=false;try{action();}catch(const std::exception&){caught=true;}CHECK(caught);++guards;}
std::vector<std::uint8_t> bytes(wr::Process& p,wr::U32 at,std::size_t size){std::vector<std::uint8_t> data(size);p.memory.copy_out(at,data);return data;}
std::array<wr::U32,32> pointers(wr::Process& p){std::array<wr::U32,32> result{};for(unsigned n=0;n<32;++n)result[n]=p.memory.load(0x4be348+n*4,32);return result;}
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto image=a::initial_image();
    const auto names=a::language_default_names();CHECK(names.size()==20);
    std::set<unsigned> changed;for(const auto& name:names){CHECK(changed.insert(name.slot).second && name.slot<32 && name.chinese!=name.japanese);}
    a::Runtime chinese;chinese.load(image);auto& cp=chinese.process();
    const auto original=pointers(cp);const auto cpu=cp.cpu;const auto epoch=cp.memory.code_epoch();
    a::configure_language_names(cp,r::GameLanguage::chinese);
    CHECK(pointers(cp)==original && cp.cpu.r==cpu.r && cp.cpu.eip==cpu.eip && cp.memory.code_epoch()==epoch);
    for(const auto& name:names)CHECK(cp.read_string(original[name.slot],64)==name.chinese && original[name.slot]==name.address);
    CHECK(cp.state().reports.at("language_names")().find("\"changed_default_slots\":0")!=std::string::npos);
    rejects([&]{a::configure_language_names(cp,r::GameLanguage::japanese);});CHECK(pointers(cp)==original);
    a::Runtime japanese;japanese.load(image);auto& jp=japanese.process();
    const auto code=bytes(jp,0x401000,0xa0000-0x1000),tables=bytes(jp,0x4a1420,0x190);const auto before=pointers(jp);const auto registers=jp.cpu;
    const auto code_epoch=jp.memory.code_epoch();
    a::configure_language_names(jp,r::GameLanguage::japanese);
    CHECK(jp.read_string(jp.memory.load(0x4be348,32),64)=="\x8f\x74\x8a\xf3");
    CHECK(jp.read_string(jp.memory.load(0x4be34c,32),64)=="\x82\xa9\x82\xb8\x82\xb3");
    CHECK(jp.read_string(jp.memory.load(0x4be350,32),64)=="\x90\xe1\x8d\xd8");
    for(const auto& name:names){const auto address=jp.memory.load(0x4be348+name.slot*4,32);CHECK(address!=name.address && jp.read_string(address,64)==name.japanese);CHECK(jp.read_string(name.address,64)==name.chinese);}
    unsigned preserved{};for(unsigned n=0;n<32;++n)if(!changed.contains(n)){CHECK(jp.memory.load(0x4be348+n*4,32)==before[n]);++preserved;}
    CHECK(jp.memory.load(0x4be348+22*4,32)==jp.memory.load(0x4be348+23*4,32));
    CHECK(bytes(jp,0x401000,code.size())==code && bytes(jp,0x4a1420,tables.size())==tables && jp.memory.code_epoch()==code_epoch);
    CHECK(jp.cpu.r==registers.r && jp.cpu.eip==registers.eip && jp.native_function_entries==0 && jp.executed_instructions==0);
    rejects([&]{a::configure_language_names(jp,r::GameLanguage::japanese);});
    {a::Runtime empty;rejects([&]{a::configure_language_names(empty.process(),r::GameLanguage::japanese);});}
    for(unsigned fault=0;fault<6;++fault){
        a::Runtime invalid;invalid.load(image);auto& p=invalid.process();
        if(fault==0)p.input_sha256="unknown";
        if(fault==1)p.memory.store(0x4be348+31*4,0,32);
        if(fault==2){p.memory.protect(0x4a1000,4096,wr::Memory::Read|wr::Memory::Write);p.memory.store(names.back().address,1,8);}
        if(fault==3)p.memory.protect(0x4be000,4096,wr::Memory::Read);
        if(fault==4)p.native_function_entries=1; // Reject late setup without executing any game function.
        const auto state=pointers(p);
        rejects([&]{a::configure_language_names(p,fault==5?static_cast<r::GameLanguage>(99):r::GameLanguage::japanese);});
        CHECK(pointers(p)==state && !p.state().reports.contains("language_names"));
    }
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.language-default-names.v1\",\"checks\":"<<checks<<",\"guards\":"<<guards<<",\"changed_slots\":20,\"preserved_slots\":"<<preserved<<",\"default_chinese_unchanged\":true,\"source_strings_unchanged\":true,\"glyph_tables_and_code_unchanged\":true,\"alias_identity_preserved\":true,\"invalid_contract_not_published\":true,\"static_function_entries\":0,\"instruction_step_calls\":0,\"japanese_names\":[";
    bool comma=false;for(unsigned n=0;n<32;++n){if(comma)report<<',';comma=true;auto ptr=jp.memory.load(0x4be348+n*4,32);const auto value=ptr?jp.read_string(ptr,64):std::string{};const char* hex="0123456789abcdef";std::string encoded;for(const unsigned char c:value){encoded+=hex[c>>4];encoded+=hex[c&15];}report<<wr::quote(encoded);}
    report<<"],\"source_executable_opened\":false,\"original_resource_files_opened\":false,\"game_launched\":false,\"screen_access\":false,\"complete_gameplay_verified\":false,\"passed\":true}\n";CHECK(report);
    std::cout<<"PASS default names: verified initial RAM, paired language data, unchanged lookup/code and guarded setup.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
