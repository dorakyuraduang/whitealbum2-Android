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
bool original_manual_save(std::span<const std::uint8_t> bytes)noexcept{return bytes.size()>=4 && (bytes[0]==1 || bytes[0]==2) && !bytes[1] && !bytes[2] && !bytes[3];}
SavePreview inspect_legacy_save_preview(std::span<const std::uint8_t> bytes){
    const auto header=inspect_legacy_save_header(bytes);
    if(bytes.size()<original_save_header_bytes+65536)throw std::runtime_error("Truncated original manual-save thumbnail");SavePreview result;result.date=header.date;result.description=header.description;result.story_code=header.story_code;result.story_known=true;result.original=true;result.scene="original";
    auto image=std::make_shared<image::Raster>();image->width=image->height=128;image->rgba.resize(65536);const auto pixels=bytes.subspan(original_save_header_bytes,65536);
    for(unsigned n=0;n<65536;n+=4){image->rgba[n]=pixels[n+2];image->rgba[n+1]=pixels[n+1];image->rgba[n+2]=pixels[n];image->rgba[n+3]=pixels[n+3];}result.thumbnail=std::move(image);return result;
}
SavedScene::Bytes upgrade_legacy_scene_v1(std::span<const std::uint8_t> bytes){
    if(bytes.size()!=original_scene_v1_bytes)throw std::runtime_error("Original version-1 conversion requires a complete scene record");
    const Fields old(bytes);SavedScene::Bytes result(original_scene_v2_bytes);
    const auto copy=[&](std::size_t from,std::size_t to,std::size_t count){std::copy_n(bytes.begin()+std::ptrdiff_t(from),count,result.begin()+std::ptrdiff_t(to));};
    const auto word=[&](std::size_t at,std::uint32_t value){for(unsigned n=0;n<4;++n)result[at+n]=std::uint8_t(value>>(n*8));};
    copy(0,0,0x24);copy(0x2c,0x2c,12);
    // The old record stores destination dimensions; its loader divides by
    // the HD canvas dimensions, then supplies zero background rotation.
    word(0x24,std::bit_cast<std::uint32_t>(float(double(old.integer(0x24))/1280.0)));
    word(0x28,std::bit_cast<std::uint32_t>(float(double(old.integer(0x28))/720.0)));
    for(const auto [from,to]:{std::pair{0x48u,0x4cu},{0x50u,0x54u},{0x54u,0x58u}})copy(from,to,4);
    for(const auto [from,to]:{std::pair{0x58u,0x5cu},{0x78u,0x7cu},{0x98u,0x9cu}})copy(from,to,32);
    for(const auto [from,to]:{std::pair{0xbcu,0xc0u},{0xdcu,0xe0u},{0xfcu,0x100u},{0x11cu,0x130u},{0x13cu,0x170u},{0x17cu,0x190u},{0x15cu,0x1b0u}})copy(from,to,32);
    copy(0x1dc,0x210,1);copy(0x1de,0x212,1);copy(0x1e4,0x218,1);copy(0x1e6,0x21a,1);copy(0x1e8,0x21c,2);copy(0x1ec,0x220,2);copy(0x1ee,0x222,1024);copy(0x5ee,0x622,2);
    // The old name capacity is 32 bytes; extending its destination must not
    // accidentally accept an unterminated original field.
    if(bytes[0x1dc] && old.short_word(0x5ee) && std::find(bytes.begin()+0x5f0,bytes.begin()+0x610,std::uint8_t(0))==bytes.begin()+0x610)throw std::runtime_error("Unterminated original version-1 speaker name");
    copy(0x5f0,0x624,32);copy(0x610,0x668,4);copy(0x618,0x670,4);copy(0x61c,0x674,8*256);copy(0xe5c,0xeb4,8);copy(0xe64,0xebc,8);
    // Old restore supplies zero for disabled-option display, history group
    // and script identity. Keep that source policy rather than guessing one.
    copy(0xe6c,0xed4,4);word(0xed8,old.word(0xe70)&255u);
    for(const auto [from,to]:{std::pair{0xe74u,0xedcu},{0xe78u,0xee0u},{0xe7cu,0xee4u},{0xe80u,0xee8u}})copy(from,to,4);
    for(const auto [from,to]:{std::pair{0xe90u,0xefcu},{0xea0u,0xf0cu},{0xeb0u,0xf1cu},{0xec0u,0xf2cu}})copy(from,to,16);
    for(const auto [from,to]:{std::pair{0xed0u,0xf3cu},{0xf10u,0xf7cu},{0xf50u,0xfbcu},{0xf90u,0xffcu}})copy(from,to,64);
    copy(0x1034,0x1100,12);copy(0x1040,0x110c,4);word(0x1118,0xffffffffu);copy(0x1048,0x111c,32*0x98);
    // No old clock/calendar/novel/looping-voice restore; voice offset, adult
    // mode and extended weather amplitude are reset by the old source loader.
    return result;
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
