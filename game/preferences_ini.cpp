#include "preferences_ini.hpp"
#include <algorithm>
#include <charconv>
#include <map>
#include <string>
#include <string_view>

namespace wa2::game {
namespace {
std::string_view trim(std::string_view value){
    const auto space=[](char c){return c==' ' || c=='\t' || c=='\r';};
    while(!value.empty() && space(value.front()))value.remove_prefix(1);
    while(!value.empty() && space(value.back()))value.remove_suffix(1);return value;
}
std::string fold(std::string_view value){std::string result(value);for(auto& c:result)if(c>='A' && c<='Z')c+=char('a'-'A');return result;}
std::string text(std::span<const std::uint8_t> bytes){
    if(bytes.size()>65536)throw std::invalid_argument("Original settings INI exceeds 64 KiB");
    std::string result;result.reserve(bytes.size());
    if(bytes.size()>=2 && ((bytes[0]==255 && bytes[1]==254) || (bytes[0]==254 && bytes[1]==255))){
        if(bytes.size()%2)throw std::invalid_argument("Truncated original UTF-16 settings INI");const bool little=bytes[0]==255;
        const auto word=[&](std::size_t n){return unsigned(bytes[n+(little?0:1)])|(unsigned(bytes[n+(little?1:0)])<<8);};
        for(std::size_t n=2;n<bytes.size();n+=2){auto c=word(n);if(!c)throw std::invalid_argument("NUL in original settings INI");
            if(c>=0xd800 && c<=0xdbff){if(n+3>=bytes.size() || word(n+2)<0xdc00 || word(n+2)>0xdfff)throw std::invalid_argument("Invalid original settings UTF-16 pair");n+=2;}
            else if(c>=0xdc00 && c<=0xdfff)throw std::invalid_argument("Invalid original settings UTF-16 pair");
            // Original keys and integer values are ASCII. Non-ASCII comments
            // and unrelated sections need no game font/encoding dependency.
            result.push_back(c<128?char(c):char(128));
        }
    }else{
        if(bytes.size()>=3 && bytes[0]==239 && bytes[1]==187 && bytes[2]==191)bytes=bytes.subspan(3);
        for(auto c:bytes){if(!c)throw std::invalid_argument("NUL in original settings INI");result.push_back(char(c));}
    }
    return result;
}
class Ini {
    std::map<std::string,std::string,std::less<>> fields_;
public:
    explicit Ini(std::span<const std::uint8_t> bytes){
        const auto decoded=text(bytes);std::string_view rest=decoded;bool selected{};unsigned lines{};
        while(!rest.empty()){
            if(++lines>4096)throw std::invalid_argument("Original settings INI has too many lines");const auto end=rest.find('\n');auto line=trim(rest.substr(0,end));
            if(line.size()>4096)throw std::invalid_argument("Original settings INI line exceeds 4096 bytes");
            if(end==std::string_view::npos)rest={};else rest.remove_prefix(end+1);
            if(line.empty() || line.front()==';' || line.front()=='#')continue;
            if(line.front()=='['){const auto close=line.find(']');if(close==std::string_view::npos || !trim(line.substr(close+1)).empty())throw std::invalid_argument("Invalid original settings INI section");selected=fold(trim(line.substr(1,close-1)))=="default";continue;}
            if(!selected)continue;const auto equals=line.find('=');if(equals==std::string_view::npos)throw std::invalid_argument("Invalid original settings INI assignment");
            auto key=trim(line.substr(0,equals)),value=trim(line.substr(equals+1));if(key.empty())throw std::invalid_argument("Empty original settings INI key");
            if(value.size()>=2 && ((value.front()=='\"' && value.back()=='\"') || (value.front()=='\'' && value.back()=='\'')))value=trim(value.substr(1,value.size()-2));
            fields_.try_emplace(fold(key),value);
        }
    }
    std::optional<std::int32_t> get(std::string_view key)const{
        const auto found=fields_.find(key);if(found==fields_.end())return {};auto value=std::string_view(found->second);std::int32_t result{};
        if(value.starts_with('+'))value.remove_prefix(1);const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
        if(value.empty() || parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size())throw std::invalid_argument("Invalid original settings integer: "+std::string(key));return result;
    }
    std::int32_t range(std::string_view key,std::int32_t previous,std::int32_t low,std::int32_t high)const{
        const auto value=get(key);if(!value)return previous;if(*value<low || *value>high)throw std::invalid_argument("Original setting outside native support: "+std::string(key)+"="+std::to_string(*value)+" (expected "+std::to_string(low)+".."+std::to_string(high)+")");return *value;
    }
    bool boolean(std::string_view key,bool previous)const{return range(key,previous?1:0,0,1)!=0;}
};
}
Preferences import_preferences_ini(std::span<const std::uint8_t> system,std::span<const std::uint8_t> display){
    const Ini source(system),device(display);Preferences next;
    next.master=source.range("all_vol",next.master,0,256);next.music=source.range("bgm_vol",next.music,0,256);
    next.effects=source.range("se_vol",next.effects,0,256);next.speech=source.range("voice_vol",next.speech,0,256);
    next.audio_muted[0]=!source.boolean("all_sound",true);next.audio_muted[1]=!source.boolean("bgm",true);next.audio_muted[2]=!source.boolean("se",true);next.voice_enabled=source.boolean("voice",true);
    next.skin=source.range("win_color",next.skin,0,9);next.window_alpha=source.range("win_alpha",next.window_alpha,0,256);
    next.picture_alpha=source.range("win_alpha_vis",next.picture_alpha,0,256);next.novel_alpha=source.range("win_alpha_novel",next.novel_alpha,0,256);next.button_alpha=source.range("win_alpha_command",next.button_alpha,0,256);
    next.window=source.boolean("win_disp",next.window);next.confirmation=source.boolean("yes_no",next.confirmation);next.right_click=source.range("r_click",next.right_click,0,2);
    next.cut_voice_on_advance=source.boolean("page_voice",next.cut_voice_on_advance);next.mute_adult=source.boolean("ero_voice",next.mute_adult);
    for(unsigned n=0;n<next.character_voice.size();++n)next.character_voice[n]=source.boolean("char_voice"+std::to_string(n),next.character_voice[n]);
    next.skip_read_only=!source.boolean("msg_cut_optin",false);next.auto_ticks=source.range("auto_max",next.auto_ticks,0,36000);
    // The original INI stores a level, while native preferences store the
    // actual movie filename suffix. Cached 413a9c..413b72 maps 2/1/0 to 0/1/2.
    const auto movie_level=source.range("mov_lv",2,0,2);
    next.movie_quality=movie_level==2?0:movie_level==1?1:2;
    // Cached 433030..4330a2 scales each persisted integer by one half.
    if(const auto value=source.get("wait")){if(*value!=0 && *value!=2)throw std::invalid_argument("Unsupported original transition wait setting");next.transition_wait=*value!=0;}
    if(const auto value=source.get("msg_wait")){switch(*value){case 0:next.text_step=0;break;case 1:next.text_step=4;break;case 2:next.text_step=2;break;case 4:next.text_step=1;break;default:throw std::invalid_argument("Unsupported original message wait setting");}}
    (void)source.range("wheel",0,0,0);
    next.menu_alt=source.boolean("menu",false);
    next.fullscreen=!device.boolean("window_mode",true);
    const auto width=device.get("win_size_w"),height=device.get("win_size_h");
    if(width || height){if(!width || !height)throw std::invalid_argument("Incomplete original window dimensions");
        if(*width==1280 && *height==720)next.window_size=0;else if(*width==1024 && *height==576)next.window_size=1;else if(*width==800 && *height==450)next.window_size=2;else throw std::invalid_argument("Unsupported original window dimensions");}
    next.validate();return next;
}
}
