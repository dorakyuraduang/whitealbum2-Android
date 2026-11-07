#pragma once
#include <array>
#include <optional>
#include <string_view>

namespace wa2::resource {
// Logical read-only source directories relative to the authorized data root.
// The OS adapters enumerate these directories; common policy selects one bank.
inline constexpr std::array<std::string_view,4> legacy_save_directories{
    "", "Documents/Leaf/WHITE ALBUM2/",
    "Documents/Leaf/WHITE ALBUM2 -closing chapter-/",
    "Documents/Leaf/WHITE ALBUM2 -introductory chapter-/"};
struct LegacySavePath {unsigned directory{},slot{};};
inline std::optional<LegacySavePath> legacy_save_path(std::string_view path)noexcept{
    unsigned directory{};if(path.find('/')!=std::string_view::npos){for(directory=1;directory<legacy_save_directories.size();++directory)if(path.starts_with(legacy_save_directories[directory])){path.remove_prefix(legacy_save_directories[directory].size());break;}if(directory==legacy_save_directories.size())return {};}
    std::array<char,11> name{};if(path.size()>name.size())return {};for(unsigned n=0;n<path.size();++n){const auto ch=path[n];name[n]=ch>='A' && ch<='Z'?char(ch+32):ch;}
    const std::string_view folded(name.data(),path.size());if(folded=="save_q.sav")return LegacySavePath{directory,100};
    if(folded.size()!=11 || folded.substr(0,5)!="save_" || folded.substr(7)!=".sav" || folded[5]<'0' || folded[5]>'9' || folded[6]<'0' || folded[6]>'9')return {};
    return LegacySavePath{directory,unsigned(folded[5]-'0')*10+unsigned(folded[6]-'0')};
}
}
