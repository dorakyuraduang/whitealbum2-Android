#pragma once
#include "host.hpp"
#include <sstream>
#include <string_view>
namespace wa2::window::json {
inline std::string icon(const std::optional<IconResource>& value){
    if(!value)return "null";
    constexpr char hex[]="0123456789abcdef";std::ostringstream out;
    out<<"{\"width\":"<<value->width<<",\"height\":"<<value->height<<",\"dib\":\"";
    for(const auto byte:value->dib)out<<hex[byte>>4]<<hex[byte&15];out<<"\"}";return out.str();
}
// UI text is UTF-16, not a byte string. Escaping its code units produces
// ASCII JSON that Java decodes once, including supplementary characters.
inline std::string text(std::u16string_view value){
    constexpr char hex[]="0123456789abcdef";std::string out;out.reserve(value.size()+2);out+='"';
    for(const auto c:value){
        if(c=='"' || c=='\\'){out+='\\';out+=char(c);}
        else if(c<32 || c>=127){out+="\\u";for(int shift=12;shift>=0;shift-=4)out+=hex[(unsigned(c)>>shift)&15];}
        else out+=char(c);
    }
    out+='"';return out;
}
inline std::string dialog(const DialogState& d){
    std::ostringstream out;out<<"{\"token\":"<<d.token<<",\"owner\":"<<d.owner<<",\"title\":"<<text(d.definition.title)<<",\"visible\":"<<(d.visible?"true":"false")<<",\"width\":"<<d.definition.bounds[2]<<",\"height\":"<<d.definition.bounds[3]<<",\"controls\":[";
    bool first=true;for(const auto& c:d.controls){if(!first)out<<',';first=false;std::u16string type;if(const auto* ordinal=std::get_if<std::uint16_t>(&c.definition.type)){const auto digits=std::to_string(*ordinal);type=u"#";type.append(digits.begin(),digits.end());}else if(const auto* name=std::get_if<std::u16string>(&c.definition.type))type=*name;
        out<<"{\"id\":"<<c.definition.id<<",\"token\":"<<c.token<<",\"type\":"<<text(type)<<",\"text\":"<<text(c.text)<<",\"style\":"<<c.definition.style<<",\"bounds\":[";
        for(unsigned n=0;n<4;++n){if(n)out<<',';out<<c.definition.bounds[n];}out<<"],\"check\":"<<c.check<<",\"position\":"<<c.position<<",\"minimum\":"<<c.minimum<<",\"maximum\":"<<c.maximum<<",\"selection\":"<<c.selection<<",\"enabled\":"<<(c.enabled?"true":"false")<<",\"visible\":"<<(c.visible?"true":"false")<<",\"items\":[";
        bool first_item=true;for(const auto& item:c.items){if(!first_item)out<<',';first_item=false;out<<text(item);}out<<"],\"icon\":"<<icon(c.icon)<<'}';}out<<"]}";return out.str();
}
inline std::string menu(const std::vector<MenuItem>& items){
    std::ostringstream out;out<<'[';bool first=true;for(const auto& item:items){if(!first)out<<',';first=false;out<<"{\"id\":"<<item.id<<",\"type\":"<<item.type<<",\"state\":"<<item.state<<",\"text\":"<<text(item.text)<<",\"children\":"<<menu(item.children)<<'}';}out<<']';return out.str();
}
}
