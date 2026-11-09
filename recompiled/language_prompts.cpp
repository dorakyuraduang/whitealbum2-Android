#include "language_prompts.hpp"
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace wa2::aot {
namespace {
// Addresses and original bytes are contracts from the verified initial RAM,
// not runtime text guesses. Japanese wording uses the authorized WA2.exe
// metadata. English uses matching ASCII prompts, including the alternate
// load question that the supplied English EXE still leaves in Japanese.
constexpr MenuPrompt prompts[]{
    {0x4a2c1cu,"\x3c\x46\x32\x30\x81\x40\x81\x40\x81\x40\x99\x70\x89\xcf\x91\xd8\x8d\x5c\x91\x94\x81\x40\x81\x40\x81\x40\x81\x40\x0a\x81\x40\x81\x40\x94\xbb\x8d\x73\x8b\x4c\x81\x48\x81\x40\x81\x40\x3e",
     "\x3c\x46\x32\x30\x82\xb1\x82\xcc\x83\x66\x81\x5b\x83\x5e\x82\xf0\x83\x8d\x81\x5b\x83\x68\x82\xb5\x82\xdc\x82\xb7\x0a\x82\xe6\x82\xeb\x82\xb5\x82\xa2\x82\xc5\x82\xb7\x82\xa9\x81\x48\x3e","<F24Load this data?\nAre you sure?>"},
    {0x4a2dc4u,"\x3c\x46\x32\x30\x81\x40\x81\x40\x81\x40\x8d\x5c\x91\x94\x98\x6d\x8a\xe0\x8f\x62\x8a\x85\x81\x40\x81\x40\x81\x40\x81\x40\x3e",
     "\x3c\x46\x32\x30\x81\x40\x83\x66\x81\x5b\x83\x5e\x82\xf0\x83\x8d\x81\x5b\x83\x68\x82\xb5\x82\xdc\x82\xb5\x82\xbd\x81\x40\x3e","<F24File Loaded>"},
    {0x4a2de4u,"\x3c\x46\x32\x30\x20\x81\x40\x81\x40\x81\x40\x8d\x5c\x91\x94\x8d\xe5\x89\x97\x8d\x5c\x81\x40\x81\x40\x81\x40\x81\x40\x3e",
     "\x3c\x46\x32\x30\x20\x81\x40\x83\x66\x81\x5b\x83\x5e\x82\xf0\x95\xdb\x91\xb6\x82\xb5\x82\xdc\x82\xb5\x82\xbd\x81\x40\x3e","<F24File Saved>"},
    {0x4a2e10u,"\x3c\x46\x32\x30\x81\x40\x81\x40\x81\x40\x81\x40\x98\x6d\x8a\xe0\x8d\x5c\x91\x94\x81\x42\x81\x40\x81\x40\x81\x40\x0a\x81\x40\x81\x40\x81\x40\x81\x40\x94\xbb\x8d\x73\x8b\x4c\x81\x48\x81\x40\x81\x40\x3e",
     "\x3c\x46\x32\x30\x81\x40\x83\x66\x81\x5b\x83\x5e\x82\xf0\x83\x8d\x81\x5b\x83\x68\x82\xb5\x82\xdc\x82\xb7\x81\x42\x0a\x81\x40\x81\x40\x82\xe6\x82\xeb\x82\xb5\x82\xa2\x82\xc5\x82\xb7\x82\xa9\x81\x48\x3e","<F24Load file from this slot?>"},
    {0x4a2e44u,"\x3c\x46\x32\x30\x81\x40\x81\x40\x81\x40\x8d\x5c\x91\x94\x8d\x9f\x97\xc8\x97\xdb\x94\x6d\x81\x42\x81\x40\x81\x40\x81\x40\x0a\x81\x40\x81\x40\x81\x40\x81\x40\x94\xbb\x8d\x73\x8b\x4c\x81\x48\x81\x40\x81\x40\x3e",
     "\x3c\x46\x32\x30\x83\x66\x81\x5b\x83\x5e\x82\xf0\x8f\xe3\x8f\x91\x82\xab\x95\xdb\x91\xb6\x82\xb5\x82\xdc\x82\xb7\x81\x42\x0a\x81\x40\x81\x40\x82\xe6\x82\xeb\x82\xb5\x82\xa2\x82\xc5\x82\xb7\x82\xa9\x81\x48\x3e","<F24Overwrite file in this slot?>"},
    {0x4a2f00u,"\x3c\x46\x32\x30\x81\x40\x81\x40\x81\x40\x8e\xa6\x99\xb6\x98\x6d\x8a\xe0\x8d\x5c\x91\x94\x81\x42\x81\x40\x81\x40\x0a\x81\x40\x81\x40\x81\x40\x81\x40\x94\xbb\x8d\x73\x8b\x4c\x81\x48\x81\x40\x81\x40\x3e",
     "\x3c\x46\x32\x30\x81\x40\x83\x4e\x83\x43\x83\x62\x83\x4e\x83\x8d\x81\x5b\x83\x68\x82\xb5\x82\xdc\x82\xb7\x81\x42\x0a\x81\x40\x81\x40\x82\xe6\x82\xeb\x82\xb5\x82\xa2\x82\xc5\x82\xb7\x82\xa9\x81\x48\x3e","<F24Load Quicksave?>"},
    {0x4a2f34u,"\x3c\x46\x32\x30\x81\x40\x8f\x70\x90\xfc\x98\x48\x95\xfa\x99\x94\x8c\x60\x9b\x96\x97\xf3\x98\x48\x95\xfa\x81\x40\x0a\x81\x40\x81\x40\x81\x40\x81\x40\x94\xbb\x8d\x73\x8b\x4c\x81\x48\x81\x40\x81\x40\x3e",
     "\x3c\x46\x32\x30\x82\xb7\x82\xd7\x82\xc4\x8f\x89\x8a\xfa\x90\xdd\x92\xe8\x82\xc9\x96\xdf\x82\xb5\x82\xdc\x82\xb7\x0a\x81\x40\x81\x40\x82\xe6\x82\xeb\x82\xb5\x82\xa2\x82\xc5\x82\xb7\x82\xa9\x81\x48\x3e","<F24Are you sure you want\nto restore defaults?>"},
};
constexpr std::string_view source_identity=
    "246a263b965147a274fa59cdd7a72f6be10ba063d8cf4e61e74baefb56f29327";
std::string_view selected(const MenuPrompt& prompt,resource::GameLanguage language){
    switch(language){
    case resource::GameLanguage::chinese:return prompt.chinese;
    case resource::GameLanguage::japanese:return prompt.japanese;
    case resource::GameLanguage::english:return prompt.english;
    }
    throw std::invalid_argument("Unsupported menu-prompt language");
}
}
std::span<const MenuPrompt> language_menu_prompts()noexcept{return prompts;}
void configure_language_prompts(wr::Process& process,resource::GameLanguage language){
    const auto profile=resource::language_profile(language);
    if(!process.entry || process.native_function_entries || process.executed_instructions ||
       process.state().reports.contains("language_prompts"))
        throw std::logic_error("Menu-prompt language must be configured once before game execution");
    if(process.input_sha256!=source_identity)
        throw std::runtime_error("Unknown source menu-prompt layout");
    std::array<std::array<std::uint8_t,64>,std::size(prompts)> replacements{};
    // Validate every destination before changing any data. Fixed capacities
    // retain the original addresses used by the unchanged static functions.
    for(std::size_t n=0;n<std::size(prompts);++n){
        const auto& prompt=prompts[n];const auto text=selected(prompt,language);
        const auto size=prompt.chinese.size()+1;
        if(size>replacements[n].size() || text.size()>prompt.chinese.size() ||
           text.find('\0')!=std::string_view::npos)
            throw std::logic_error("Localized menu prompt exceeds its source capacity");
        process.memory.check(prompt.address,size,wr::Memory::Read);
        if(process.memory.permissions(prompt.address)!=wr::Memory::Read ||
           process.memory.permissions(prompt.address+std::uint32_t(size)-1)!=wr::Memory::Read ||
           process.read_string(prompt.address,size)!=prompt.chinese)
            throw std::runtime_error("Source menu-prompt contract changed at "+std::to_string(prompt.address));
        std::copy(text.begin(),text.end(),replacements[n].begin());
    }
    if(language!=resource::GameLanguage::chinese){
        // Loader initialization preserves read-only permissions. Zero padding
        // prevents suffixes of longer source strings from surviving the change.
        for(std::size_t n=0;n<std::size(prompts);++n)
            process.memory.initialize(prompts[n].address,
                std::span(replacements[n]).first(prompts[n].chinese.size()+1));
    }
    process.state().reports["language_prompts"]=[language,profile]{
        return "{\"language\":"+wr::quote(std::string(profile.tag))+
            ",\"changed_prompts\":"+std::to_string(language==resource::GameLanguage::chinese?0:std::size(prompts))+
            ",\"glyph_tables_changed\":false,\"save_files_rewritten\":false}";
    };
}
}

