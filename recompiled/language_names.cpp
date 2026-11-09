#include "language_names.hpp"
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <array>
#include <map>
#include <vector>
namespace wa2::aot {
namespace {
// Typed default-name metadata from the supplied WA2.exe, SHA256
// c395d93f74695d2bb7a4fb2fe30486a5d37715b4c30bf20451883569bba3990b.
// Its glyph lookup code and both range tables match the verified AOT input.
// The Chinese bytes/addresses remain contracts, never runtime guesses.
constexpr DefaultName names[]{
{0u,0x4a1c64u,"\x90\xcb\x8d\xef","\x8f\x74\x8a\xf3"},
{1u,0x4a1c5cu,"\x8b\x7c\x95\xab","\x82\xa9\x82\xb8\x82\xb3"},
{2u,0x4a1c54u,"\x9a\xb6\x97\x56","\x90\xe1\x8d\xd8"},
{10u,0x4a1c4cu,"\xce\xe4\xd2\xb2","\x95\x90\x96\xe7"},
{11u,0x4a1c44u,"\xc7\xd7\xd6\xbe","\x90\x65\x8e\x75"},
{12u,0x4a1c3cu,"\xd2\xc0\xd0\xf7","\x88\xcb\x8f\x8f"},
{13u,0x4a1c30u,"\xd0\xa1\xc4\xbe\xd4\xf8\xb8\xb8","\x8f\xac\x96\xd8\x91\x5d\x95\x83"},
{14u,0x4a1c24u,"\xd0\xa1\xc4\xbe\xd4\xf8\xc4\xb8","\x8f\xac\x96\xd8\x91\x5d\x95\xea"},
{15u,0x4a1c1cu,"\xd0\xa2\xba\xea","\x8d\x46\x8d\x47"},
{16u,0x4a1c14u,"\xea\xd7\xd7\xd3","\x97\x6a\x8e\x71"},
{17u,0x4a1c08u,"\xb5\xa3\xc8\xce\xbd\xcc\x8e\x9f","\x92\x53\x94\x43\x8b\xb3\x8e\x74"},
{18u,0x4a1c00u,"\xda\xc1\xb7\xc3","\x90\x7a\x96\x4b"},
{20u,0x4a1bf4u,"\xa5\xc9\xa5\xeb\xa5\xa6\xa5\xaf","\x83\x68\x83\x8b\x83\x45\x83\x4e"},
{21u,0x4a1be8u,"\xa5\xac\xa5\xa4\xa5\xa6\xa5\xb9","\x83\x4b\x83\x43\x83\x45\x83\x58"},
{22u,0x4a1bdcu,"\xa5\xaf\xa5\xec\xa5\xaa\xa5\xf3","\x83\x4e\x83\x8c\x83\x49\x83\x93"},
{23u,0x4a1bdcu,"\xa5\xaf\xa5\xec\xa5\xaa\xa5\xf3","\x83\x4e\x83\x8c\x83\x49\x83\x93"},
{24u,0x4a1bd0u,"\xa5\xec\xa5\xaf\xa5\xc8\xa9\x60\xa5\xeb","\x83\x8c\x83\x4e\x83\x67\x81\x5b\x83\x8b"},
{25u,0x4a1bc8u,"\xb1\xf8\xca\xbf","\x95\xba\x8e\x6d"},
{30u,0x4a1bbcu,"\xa5\xdf\xa5\xeb\xa5\xc7\xa5\xa3\xa5\xf3","\x83\x7e\x83\x8b\x83\x66\x83\x42\x83\x93"},
{31u,0x4a1bb4u,"\xa5\xd7\xa5\xa3\xa5\xeb","\x83\x76\x83\x42\x83\x8b"},
};
std::string_view selected_name(const DefaultName& name,resource::GameLanguage language){
    if(language==resource::GameLanguage::ukrainian)switch(name.slot){
    case 0:return "Uaqtk'";case 1:return "Kaehtra";case 2:return "Rf+tna";
    case 10:return "Sakfz";case 11:return "V'ka}'";case 12:return ";o";case 16:return "Joko";
    }
    return name.japanese;
}
}
std::span<const DefaultName> language_default_names()noexcept{return names;}
void configure_language_names(wr::Process& process,resource::GameLanguage language){
    const auto profile=resource::language_profile(language);
    if(!process.entry || process.native_function_entries || process.executed_instructions || process.state().reports.contains("language_names"))throw std::logic_error("Default-name language must be configured once before game execution");
    if(process.input_sha256!="246a263b965147a274fa59cdd7a72f6be10ba063d8cf4e61e74baefb56f29327")throw std::runtime_error("Unknown source default-name layout");
    constexpr std::uint32_t table=0x4be348;
    // The authorized English EXE retains the same 32 Japanese default slots.
    // Translated scripts may replace these names later through normal commands.
    if(language!=resource::GameLanguage::chinese){
        process.memory.check(table,128,wr::Memory::Read|wr::Memory::Write);
        for(const auto& name:names)if(process.memory.load(table+name.slot*4,32)!=name.address || process.read_string(name.address,64)!=name.chinese)throw std::runtime_error("Source default-name contract changed at slot "+std::to_string(name.slot));
        std::map<std::string_view,std::uint32_t> allocated;std::array<std::uint32_t,std::size(names)> pointers{};
        try{for(std::size_t n=0;n<std::size(names);++n){const auto text=selected_name(names[n],language);const auto found=allocated.find(text);if(found!=allocated.end())pointers[n]=found->second;else{const auto address=process.put_string(std::string(text));try{allocated.emplace(text,address);}catch(...){process.memory.release(address);throw;}pointers[n]=address;}}}
        catch(...){for(const auto& [text,address]:allocated)process.memory.release(address);throw;}
        // All destinations were checked before allocation; no guest callback
        // runs here. Original strings and instruction pages stay immutable.
        for(std::size_t n=0;n<std::size(names);++n)process.memory.store(table+names[n].slot*4,pointers[n],32);
    }
    process.state().reports["language_names"]=[language,profile]{return "{\"language\":"+wr::quote(std::string(profile.tag))+",\"changed_default_slots\":"+std::to_string(language!=resource::GameLanguage::chinese?std::size(names):0)+",\"glyph_tables_changed\":false,\"save_files_rewritten\":false}";};
    if(language==resource::GameLanguage::ukrainian)process.state().reports["ukrainian_text"]=[]{return "{\"source_semantic_sites\":3,\"runtime_code_patches\":false}";};
}
}
