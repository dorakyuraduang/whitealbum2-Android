#include "language_resources.hpp"
#include <stdexcept>

namespace wa2::resource {
std::string language_archive_name(std::string_view name){
    if(name.empty())return {};
    if(name.size()>128)throw std::invalid_argument("Language PAK filename exceeds 128 bytes");
    std::string result(name);
    for(auto& c:result){if(c>='A' && c<='Z')c=char(c+32);}
    auto file=std::string_view(result);
    if(file.starts_with("ic/"))file.remove_prefix(3);
    if(file.size()<5 || file.front()=='.' || !file.ends_with(".pak"))
        throw std::invalid_argument("Select an original-format script/font PAK in the game directory or IC");
    for(const auto c:file)if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.'))
        throw std::invalid_argument("Language PAK needs an ASCII filename in the game directory or IC");
    return result;
}
void validate_language_resources(const LanguageResources& value){
    language_archive_name(value.script);language_archive_name(value.font);
}
bool language_launch_option(std::span<const std::string_view> arguments,std::size_t& at,LanguageResources& value){
    const auto option=arguments[at];
    if(option=="--no-chinese-subtitles"){value.chinese_subtitles=false;return true;}
    if(option=="--chinese-subtitles"){value.chinese_subtitles=true;return true;}
    if(option!="--script-pak" && option!="--font-pak")return false;
    if(at+1>=arguments.size())throw std::invalid_argument("Missing PAK filename after "+std::string(option));
    const auto name=language_archive_name(arguments[++at]);
    if(option=="--script-pak")value.script=name;else value.font=name;
    return true;
}
}
