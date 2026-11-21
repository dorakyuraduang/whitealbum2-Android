#include "special_subtitles.hpp"
#include "raster_image.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace wa2::subtitles {
namespace {
struct EncodedCue {std::uint32_t begin,end;std::vector<std::uint16_t> text;};
using EncodedTracks=std::map<std::uint32_t,std::vector<EncodedCue>>;
struct Reader {
    std::span<const std::uint8_t> bytes;std::size_t at{},cues{},glyphs{};
    std::uint32_t word(unsigned width=4){if(width>bytes.size()-at)throw std::runtime_error("Truncated SC subtitle data");std::uint32_t value{};for(unsigned n=0;n<width;++n)value|=std::uint32_t(bytes[at++])<<(n*8);return value;}
    void text(std::vector<std::uint16_t>& out,std::size_t capacity,unsigned offset,bool secondary=false){
        const auto size=word();if(size>256 || glyphs+size>131072)throw std::runtime_error("SC subtitle text exceeds its bound");glyphs+=size;
        if(size && secondary && !out.empty())out.push_back(line_break);
        for(unsigned n=0;n<size;++n){const auto id=word(2);if(id>=capacity)throw std::runtime_error("SC subtitle glyph is outside its font");out.push_back(std::uint16_t(id+offset));}
    }
    EncodedTracks tracks(std::size_t capacity,unsigned offset,bool dialogue){
        const auto count=word();if(count>1024)throw std::runtime_error("SC subtitle track limit exceeded");EncodedTracks tracks;
        for(unsigned n=0;n<count;++n){const auto key=word(),size=word();if((key>>16)<80 || !size || cues+size>8192 || tracks.contains(key))throw std::runtime_error("Invalid SC subtitle track");cues+=size;auto& track=tracks[key];std::uint32_t previous{};
            for(unsigned j=0;j<size;++j){EncodedCue cue{word(),word(),{}};if(cue.begin<previous || cue.begin>=cue.end || cue.end>24u*60*60*1000)throw std::runtime_error("Invalid SC subtitle interval");previous=cue.end;text(cue.text,capacity,offset);if(dialogue)text(cue.text,capacity,offset,true);if(cue.text.empty())throw std::runtime_error("Empty SC subtitle cue");track.push_back(std::move(cue));}
        }return tracks;
    }
};
image::Raster font(std::span<const std::uint8_t> bytes){
    if(bytes.size()<18 || bytes.size()>8*1024*1024)throw std::runtime_error("Invalid SC subtitle font size");
    const auto layout=image::inspect_game_tga(bytes);if(layout.width!=960 || !layout.height || layout.height%32 || layout.height>2048 || layout.bits!=32)throw std::runtime_error("SC subtitle fonts need the original 30-column, 32-pixel RGBA atlas");
    return image::decode_game_tga(bytes);
}
}
std::shared_ptr<const Catalogue> compile_special_binary(std::span<const std::uint8_t> data,std::span<const std::uint8_t> dialogue_font,std::span<const std::uint8_t> song_font){
    if(data.size()<12 || data.size()>maximum_yaml_bytes)throw std::runtime_error("SC subtitle data exceeds its bound");
    Reader reader{data};if(reader.word()!=0x376e7378)throw std::runtime_error("Unsupported SC subtitle format");
    auto dialogue=font(dialogue_font),song=font(song_font);const auto first=dialogue.height/32*30,total=(dialogue.height+song.height)/32*30;
    if(dialogue.height+song.height>2048)throw std::runtime_error("Combined SC subtitle atlas exceeds GLES3 bounds");
    const auto primary=reader.tracks(first,0,true),lyrics=reader.tracks(song.height/32*30,first,false);
    if(reader.at!=data.size() || primary.empty())throw std::runtime_error("Incomplete or trailing SC subtitle data");
    auto out=std::shared_ptr<Catalogue>(new Catalogue);out->glyph_count_=total;out->atlas_=std::make_unique<Atlas>();out->atlas_->spec={32,30,(dialogue.height+song.height)/32,28,2,28};
    out->atlas_->image={960,dialogue.height+song.height,std::move(dialogue.rgba)};out->atlas_->image.rgba.insert(out->atlas_->image.rgba.end(),song.rgba.begin(),song.rgba.end());
    // Overlay both SC text rows and lyrics on the same shared subtitle canvas.
    // Merge only at cue boundaries, never by polling or regenerating per frame.
    std::set<std::uint32_t> keys;for(const auto& [key,cues]:primary)keys.insert(key);for(const auto& [key,cues]:lyrics)keys.insert(key);
    std::map<std::uint32_t,std::vector<EncodedCue>> merged;std::size_t cue_count{},glyph_count{};const std::vector<EncodedCue> empty;
    for(const auto key:keys){const auto& a=primary.contains(key)?primary.at(key):empty;const auto& b=lyrics.contains(key)?lyrics.at(key):empty;std::vector<std::uint32_t> times;
        for(const auto* track:{&a,&b})for(const auto& cue:*track){times.push_back(cue.begin);times.push_back(cue.end);}std::sort(times.begin(),times.end());times.erase(std::unique(times.begin(),times.end()),times.end());std::size_t ai{},bi{};
        for(std::size_t n=0;n+1<times.size();++n){const auto begin=times[n],end=times[n+1];EncodedCue cue{begin,end,{}};
            for(const auto& pair:{std::pair{&a,&ai},std::pair{&b,&bi}}){const auto& track=*pair.first;auto& at=*pair.second;while(at<track.size() && track[at].end<=begin)++at;if(at<track.size() && track[at].begin<=begin){if(!cue.text.empty())cue.text.push_back(line_break);cue.text.insert(cue.text.end(),track[at].text.begin(),track[at].text.end());}}
            if(cue.text.empty())continue;if(cue.text.size()>512 || ++cue_count>8192 || (glyph_count+=cue.text.size())>262144)throw std::runtime_error("Merged SC subtitle limits exceeded");merged[key].push_back(std::move(cue));
        }
    }
    out->glyphs_.reserve(glyph_count);out->cues_.reserve(cue_count);out->tracks_.reserve(merged.size());
    for(const auto& [key,track]:merged){const auto start=out->cues_.size();for(const auto& cue:track){const auto at=out->glyphs_.size();out->glyphs_.insert(out->glyphs_.end(),cue.text.begin(),cue.text.end());out->cues_.push_back({cue.begin,cue.end,{out->glyphs_.data()+at,cue.text.size()}});}out->tracks_.push_back({key,{out->cues_.data()+start,track.size()}});}
    return out;
}
}
