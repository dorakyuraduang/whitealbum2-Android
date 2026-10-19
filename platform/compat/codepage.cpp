#include "codepage.hpp"
#include "../../generated/codepages/nls_snapshot.hpp"
#include <algorithm>
namespace wa2::platform::compat {
std::uint16_t character_type(char16_t scalar,std::uint32_t type){
    const auto* range=std::lower_bound(std::begin(scalar_ranges),std::end(scalar_ranges),scalar,[](const auto& range,auto value){return range.last<value;});
    if(range==std::end(scalar_ranges) || scalar<range->first)throw EncodingError("missing Unicode character metadata");
    if(type==1)return range->ctype1;if(type==2)return range->ctype2;if(type==4)return range->ctype3;throw EncodingError("invalid character type class");
}
char16_t character_case(char16_t scalar,bool upper){
    const auto table=upper?std::span<const NlsCase>(scalar_upper):std::span<const NlsCase>(scalar_lower);
    const auto it=std::lower_bound(table.begin(),table.end(),scalar,[](const auto& row,auto value){return row.from<value;});return it!=table.end() && it->from==scalar?char16_t(it->to):scalar;
}
namespace {
void append_scalar(std::u16string& result,std::uint32_t scalar){if(scalar<65536)result+=char16_t(scalar);else{scalar-=65536;result+=char16_t(0xd800+(scalar>>10));result+=char16_t(0xdc00+(scalar&1023));}}
std::uint32_t find(std::span<const std::uint32_t> pairs,std::uint32_t value){const auto item=std::lower_bound(pairs.begin(),pairs.end(),value<<16);return item!=pairs.end() && (*item>>16)==value?(*item&65535):0xffffffff;}
}
std::u16string decode_text(std::uint32_t cp,std::span<const std::uint8_t> bytes,bool strict){
    std::u16string result;result.reserve(bytes.size());
    if(cp==65001){for(std::size_t i=0;i<bytes.size();){const auto first=bytes[i++];if(first<128){result+=char16_t(first);continue;}
        const unsigned length=first>=0xc2 && first<=0xdf?2:first>=0xe0 && first<=0xef?3:first>=0xf0 && first<=0xf4?4:0;
        auto invalid=[&]{if(strict)throw EncodingError("invalid UTF8 sequence");result+=char16_t(0xfffd);};
        if(!length){invalid();continue;}std::uint32_t scalar=first&((1u<<(7-length))-1);unsigned used=1;
        while(used<length && i<bytes.size() && (bytes[i]&0xc0)==0x80){if(used==1 && ((first==0xe0 && bytes[i]<0xa0) || (first==0xed && bytes[i]>=0xa0) || (first==0xf0 && bytes[i]<0x90) || (first==0xf4 && bytes[i]>=0x90)))break;
            scalar=scalar*64+(bytes[i++]&63);++used;}
        if(used!=length){invalid();continue;}append_scalar(result,scalar);
    }return result;}
    const auto* page=codepage(cp);if(!page)throw EncodingError("unsupported codepage");
    for(std::size_t at=0;at<bytes.size();++at){const auto first=bytes[at];const auto single=page->single[first];if(single!=0xffff){result+=char16_t(single);continue;}
        if(at+1<bytes.size()){const auto paired=find(page->pairs,(unsigned(first)<<8)|bytes[at+1]);if(paired!=0xffffffff){result+=char16_t(paired);++at;continue;}}
        if(strict)throw EncodingError("invalid multibyte sequence");result+=char16_t(0xfffd);
    }return result;
}
Encoded encode_text(std::uint32_t cp,std::u16string_view text,std::span<const std::uint8_t> fallback,bool strict){
    Encoded result;result.bytes.reserve(text.size()*2);const auto* page=cp==65001?nullptr:codepage(cp);if(cp!=65001 && !page)throw EncodingError("unsupported codepage");
    auto substitute=[&]{if(strict)throw EncodingError("unencodable scalar");result.substituted=true;if(fallback.empty())result.bytes.push_back('?');else result.bytes.insert(result.bytes.end(),fallback.begin(),fallback.end());};
    for(std::size_t at=0;at<text.size();++at){std::uint32_t scalar=text[at];if(scalar>=0xd800 && scalar<0xdc00 && at+1<text.size() && text[at+1]>=0xdc00 && text[at+1]<0xe000){scalar=0x10000+((scalar-0xd800)<<10)+(text[++at]-0xdc00);}
        else if(scalar>=0xd800 && scalar<0xe000){if(strict)throw EncodingError("unpaired UTF16 surrogate");if(cp!=65001){substitute();continue;}scalar=0xfffd;}
        if(cp==65001){if(scalar<0x80)result.bytes.push_back(std::uint8_t(scalar));else if(scalar<0x800){result.bytes.push_back(std::uint8_t(0xc0|(scalar>>6)));result.bytes.push_back(std::uint8_t(0x80|(scalar&63)));}
            else{if(scalar>=0x10000)result.bytes.push_back(std::uint8_t(0xf0|(scalar>>18)));result.bytes.push_back(std::uint8_t((scalar<0x10000?0xe0:0x80)|((scalar>>12)&(scalar<0x10000?15:63))));result.bytes.push_back(std::uint8_t(0x80|((scalar>>6)&63)));result.bytes.push_back(std::uint8_t(0x80|(scalar&63)));}continue;}
        const auto encoded=scalar<65536?find(page->reverse,scalar):0xffffffff;if(encoded==0xffffffff){substitute();continue;}
        if(encoded>=256)result.bytes.push_back(std::uint8_t(encoded>>8));result.bytes.push_back(std::uint8_t(encoded));
    }return result;
}
}
