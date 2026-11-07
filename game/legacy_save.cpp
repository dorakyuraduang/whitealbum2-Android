#include "legacy_save.hpp"
#include "checkpoint_codec.hpp"
#include <algorithm>
#include <bit>

namespace wa2::game {
namespace {
class Fields {
    std::span<const std::uint8_t> data_;
public:
    explicit Fields(std::span<const std::uint8_t> bytes):data_(bytes){}
    std::uint32_t word(std::size_t at)const{if(at>data_.size() || data_.size()-at<4)throw std::runtime_error("Truncated original save field");checkpoint::Reader in(data_.subspan(at,4));return in.u32();}
    std::int32_t integer(std::size_t at)const{return std::bit_cast<std::int32_t>(word(at));}
    unsigned short_word(std::size_t at)const{if(at>data_.size() || data_.size()-at<2)throw std::runtime_error("Truncated original save word");return unsigned(data_[at])|(unsigned(data_[at+1])<<8);}
};
void volume(std::int32_t value){if(value<0 || value>255)throw std::runtime_error("Original saved audio volume exceeds its range");}
}
LegacySaveHeader inspect_legacy_save_header(std::span<const std::uint8_t> bytes){
    if(bytes.size()<original_save_header_bytes)throw std::runtime_error("Truncated original manual-save header");const Fields data(bytes);LegacySaveHeader result;
    result.version=data.word(0);if(result.version!=1 && result.version!=2)throw std::runtime_error("Unsupported original manual-save version");
    result.story_code=data.word(4);if(result.story_code>9999999)throw std::runtime_error("Original save story classification exceeds its range");
    result.date.year=int(data.short_word(8));result.date.month=int(data.short_word(10));result.date.weekday=int(data.short_word(12));result.date.day=int(data.short_word(14));
    result.date.hour=int(data.short_word(16));result.date.minute=int(data.short_word(18));result.date.second=int(data.short_word(20));result.millisecond=data.short_word(22);result.date.validate();
    if(result.millisecond>999)throw std::runtime_error("Original save timestamp millisecond exceeds its range");
    const auto field=bytes.subspan(24,64);const auto end=std::find(field.begin(),field.end(),std::uint8_t(0));if(end==field.end())throw std::runtime_error("Unterminated original save description");
    result.description.assign(reinterpret_cast<const char*>(field.data()),std::size_t(end-field.begin()));return result;
}
LegacyAudioSave import_legacy_scene_audio(std::span<const std::uint8_t> bytes){
    if(bytes.size()!=original_scene_v2_bytes)throw std::runtime_error("Original audio conversion requires one complete version-2 scene record");const Fields data(bytes);LegacyAudioSave result;
    // The original loader skips idle and fading-out banks. It restarts each
    // other bank without preserving a decoder cursor. Only one retained music
    // owner is supported by the shared engine; ambiguous overlays must not be
    // silently collapsed into a guessed song.
    std::int32_t song=-1,mode{},gain{};
    for(unsigned n=0;n<4;++n){const auto id=data.integer(0xefc+n*4),state=data.integer(0xf0c+n*4);if(id==-1 || state==0 || state==4)continue;
        if(id<0 || id>=200 || (state!=1 && state!=3 && state!=5))throw std::runtime_error("Unsupported original saved music identity or phase");
        if(song!=-1)throw std::runtime_error("Original save has multiple simultaneous music owners");song=id;mode=data.integer(0xf1c+n*4);gain=data.integer(0xf2c+n*4);volume(gain);
    }
    checkpoint::Writer music;music.i32(song);if(song!=-1){music.boolean(mode==0);music.i32(gain);}result.music=music.seal("LSMR");
    checkpoint::Writer effects;
    for(unsigned n=0;n<16;++n){const bool resume=data.word(0xf3c+n*4)!=0 && data.word(0xfbc+n*4)!=0;effects.boolean(resume);if(!resume)continue;
        const auto sound=data.integer(0xf7c+n*4),gain=data.integer(0xffc+n*4);if(sound<0)throw std::runtime_error("Invalid original saved sound-effect identity");volume(gain);effects.i32(sound);effects.i32(gain);
    }
    result.effects=effects.seal("LSER");
    checkpoint::Writer voices;voices.u64(0);
    for(unsigned n=0;n<4;++n){const bool resume=data.word(0x107c+n*4)!=0;voices.boolean(resume);if(!resume)continue;
        const auto speaker=data.integer(0x108c+n*4),chapter=data.integer(0x109c+n*4),line=data.integer(0x10ac+n*4),gain=data.integer(0x10bc+n*4);
        if(speaker<0 || speaker>99 || chapter<0 || chapter>9999 || line<0 || line>9999)throw std::runtime_error("Invalid original saved looping voice identity");volume(gain);
        voices.i32(speaker);voices.i32(chapter);voices.i32(line);voices.i32(gain);
    }
    result.voices=voices.seal("LSVR");return result;
}
}
