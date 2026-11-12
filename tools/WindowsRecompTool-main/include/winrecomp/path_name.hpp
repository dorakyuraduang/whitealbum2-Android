#pragma once
#include <filesystem>
#include <string>
#include <string_view>
namespace wr {
inline std::filesystem::path utf8_file_path(std::string_view text){return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()),text.size()));}
inline std::string utf8_file_name(const std::filesystem::path& path){const auto text=path.generic_u8string();return {text.begin(),text.end()};}
inline bool utf8_path_name(std::string_view text){
    for(std::size_t i=0;i<text.size();){const auto first=static_cast<unsigned char>(text[i++]);if(first<128)continue;
        const unsigned count=first>=0xc2 && first<=0xdf?1:first>=0xe0 && first<=0xef?2:first>=0xf0 && first<=0xf4?3:0;
        if(!count || text.size()-i<count)return false;unsigned value=first&((1u<<(6-count))-1);
        for(unsigned n=0;n<count;++n){const auto next=static_cast<unsigned char>(text[i++]);if((next&0xc0)!=0x80)return false;value=(value<<6)|(next&63);}
        if((count==1 && value<128) || (count==2 && value<2048) || (count==3 && value<65536) || value>0x10ffff || (value>=0xd800 && value<=0xdfff))return false;
    }return true;
}
inline bool dbcs_path_pair(std::string_view text,std::size_t i){if(i+1>=text.size())return false;const auto lead=static_cast<unsigned char>(text[i]),trail=static_cast<unsigned char>(text[i+1]);return lead>=0x81 && lead<=0xfe && trail>=0x40 && trail<=0xfe && trail!=0x7f;}
// A DBCS trail byte may be a backslash or an ASCII capital. Neither is a path
// separator/case-foldable character inside the encoded character itself.
inline std::string normalize_windows_name(std::string_view text,bool fold=false){
    std::string result(text);const bool utf8=utf8_path_name(text);
    for(std::size_t i=0;i<result.size();++i){if(!utf8 && dbcs_path_pair(text,i)){++i;continue;}auto& c=result[i];if(c=='\\')c='/';else if(fold && c>='A' && c<='Z')c=char(c+32);}return result;
}
}
