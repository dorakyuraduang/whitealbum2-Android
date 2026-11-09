#include "language_resources.hpp"
#include <stdexcept>

namespace wa2::resource {
const LanguageProfile& language_profile(GameLanguage language){
    static constexpr LanguageProfile chinese{"zh-CN","ck-gal.pak","fon.pak"},japanese{"ja-JP","script.pak","fnt.pak"},english{"en-US","en.pak","fon_en.pak"},ukrainian{"uk-UA","ua.pak","font_ua.pak"};
    switch(language){case GameLanguage::chinese:return chinese;case GameLanguage::japanese:return japanese;case GameLanguage::english:return english;case GameLanguage::ukrainian:return ukrainian;}
    throw std::invalid_argument("Unsupported game language");
}
GameLanguage parse_language(std::string_view value){if(value=="zh-CN")return GameLanguage::chinese;if(value=="ja-JP")return GameLanguage::japanese;if(value=="en-US")return GameLanguage::english;if(value=="uk-UA")return GameLanguage::ukrainian;throw std::invalid_argument("Unsupported game language: "+std::string(value)+" (use zh-CN, ja-JP, en-US or uk-UA)");}
void validate_language_resources(const LanguageResources& value){
    (void)language_profile(value.language);
}
bool language_launch_option(std::span<const std::string_view> arguments,std::size_t& at,LanguageResources& value){
    if(at>=arguments.size())return false;const auto option=arguments[at];
    if(option=="--no-chinese-subtitles"){value.chinese_subtitles=false;return true;}
    if(option=="--chinese-subtitles"){value.chinese_subtitles=true;return true;}
    if(option=="--script-pak" || option=="--font-pak")throw std::invalid_argument("Custom script/font paths were removed; use --language zh-CN, ja-JP, en-US or uk-UA");
    if(option!="--language")return false;
    if(at+1>=arguments.size())throw std::invalid_argument("Missing language after --language");
    value.language=parse_language(arguments[++at]);
    return true;
}
}
