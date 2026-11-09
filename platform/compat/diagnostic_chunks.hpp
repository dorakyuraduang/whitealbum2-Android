#pragma once
#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace wa2::platform::compat {
// Some device log writers truncate long printf records. Transport bounded
// UTF-8 pieces; the collector publishes only completely assembled JSON.
template<class Emit>void emit_counter_record(std::string_view label,std::string_view json,Emit emit){
    if(label.size()>128 || json.size()>65536)throw std::length_error("Counter transport exceeds bound");
    constexpr std::size_t Limit=640;
    if(json.size()<=Limit){emit(label,json);return;}
    std::vector<std::string_view> pieces;
    for(std::size_t begin=0;begin<json.size();){auto end=std::min(begin+Limit,json.size());
        while(end<json.size() && end>begin && (static_cast<unsigned char>(json[end])&0xc0)==0x80)--end;
        if(end==begin)throw std::invalid_argument("Invalid counter UTF-8 boundary");
        pieces.push_back(json.substr(begin,end-begin));begin=end;
    }
    for(std::size_t n=0;n<pieces.size();++n){const auto part=std::string(label)+" part "+std::to_string(n+1)+"/"+std::to_string(pieces.size());emit(part,pieces[n]);}
}
}
