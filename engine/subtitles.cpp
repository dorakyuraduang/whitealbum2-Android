#include "subtitles.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace wa2::subtitles {
namespace {
#include "../third_party/wa2_subtitles/main_story.inc"
#include "../third_party/wa2_subtitles/font.inc"
}
std::span<const Track> main_story_tracks() noexcept {return subtitle_tracks;}
std::span<const char32_t> characters() noexcept {return subtitle_characters;}
const Track* find_track(std::uint32_t key) noexcept {
    const auto tracks=main_story_tracks();
    const auto it=std::lower_bound(tracks.begin(),tracks.end(),key,[](const auto& t,auto k){return t.key<k;});
    return it!=tracks.end() && it->key==key?&*it:nullptr;
}
FontSpec font_spec() noexcept {return subtitle_font;}
std::span<const std::uint8_t> font_coverage() noexcept {return subtitle_font_rle;}
FontImage decode_font(std::span<const std::uint8_t> packed,FontSpec spec,std::size_t glyphs){
    if(!spec.cell || spec.cell>40 || !spec.columns || spec.columns>32 || !spec.rows || spec.rows>64 ||
       !glyphs || glyphs>1536 || glyphs>std::size_t(spec.columns)*spec.rows)
        throw std::runtime_error("invalid embedded subtitle font dimensions");
    FontImage out{spec.cell*spec.columns,spec.cell*spec.rows,{}};
    if(out.width>2048 || out.height>2048)throw std::runtime_error("embedded subtitle font exceeds GLES3 limits");
    const auto area=std::size_t(spec.cell)*spec.cell;
    const auto size=glyphs*area*2;
    std::vector<std::uint8_t> masks; masks.reserve(size);
    for(auto run:packed){
        const auto count=unsigned(run>>4)+1;
        if(count>size-masks.size())throw std::runtime_error("overlong embedded subtitle font coverage");
        masks.insert(masks.end(),count,run&15);
    }
    if(masks.size()!=size)throw std::runtime_error("truncated embedded subtitle font coverage");
    out.rgba.resize(std::size_t(out.width)*out.height*4);
    for(std::size_t n=0;n<glyphs;++n)for(std::size_t at=0;at<area;++at){
        const auto fill=masks[(n*2)*area+at],outline=masks[(n*2+1)*area+at];
        if(fill>outline)throw std::runtime_error("invalid embedded subtitle outline coverage");
        const auto x=n%spec.columns*spec.cell+at%spec.cell;
        const auto y=n/spec.columns*spec.cell+at/spec.cell;
        const auto to=(y*out.width+x)*4;
        const auto colour=outline?std::uint8_t((unsigned(fill)*255+outline/2)/outline):0;
        out.rgba[to]=out.rgba[to+1]=out.rgba[to+2]=colour;
        out.rgba[to+3]=std::uint8_t(outline*17);
    }
    return out;
}
std::vector<GlyphQuad> layout(const Cue& cue,FontSpec spec,std::size_t glyphs){
    if(cue.glyphs.size()>512 || !spec.advance || spec.advance>40 || !spec.cell || spec.cell>40 ||
       !spec.columns || !spec.rows || spec.origin_x>spec.cell || spec.baseline>spec.cell)
        throw std::runtime_error("invalid embedded subtitle layout");
    const auto per_line=std::size_t((1280-64)/spec.advance);
    std::vector<GlyphQuad> out;out.reserve(cue.glyphs.size());
    const float width=float(spec.cell*spec.columns),height=float(spec.cell*spec.rows);
    for(std::size_t first=0;first<cue.glyphs.size();first+=per_line){
        const auto length=std::min(per_line,cue.glyphs.size()-first);
        const float x=(1280.f-float(length*spec.advance))/2-float(spec.origin_x);
        const float y=40.f+float(spec.advance)-float(spec.baseline)+float(first/per_line*(spec.advance+8));
        for(std::size_t n=0;n<length;++n){
            const auto id=cue.glyphs[first+n];if(id>=glyphs)throw std::runtime_error("unknown embedded subtitle character");
            const float left=x+float(n*spec.advance),u=float(id%spec.columns*spec.cell),v=float(id/spec.columns*spec.cell);
            out.push_back({left,y,left+spec.cell,y+spec.cell,u/width,v/height,(u+spec.cell)/width,(v+spec.cell)/height});
        }
    }
    return out;
}
bool Playback::play(std::uint32_t key,Kind kind,std::uint32_t slot,std::uint64_t now,bool replace){
    (void)current(now);
    const auto* next=find_track(key);
    if(!next || (track_ && !replace))return false;
    if((key<65536)!=(kind==Kind::effect))return false;
    track_=next;kind_=kind;slot_=slot;started_=now;++generation_;return true;
}
void Playback::stop(Kind kind,std::uint32_t slot) noexcept {
    if(track_ && kind_==kind && slot_==slot)reset();
}
const Cue* Playback::current(std::uint64_t now) noexcept {
    if(!track_)return nullptr;
    const auto elapsed=now>=started_?now-started_:0;
    if(elapsed>=track_->cues.back().end_ms){reset();return nullptr;}
    const auto cues=track_->cues;
    const auto it=std::upper_bound(cues.begin(),cues.end(),elapsed,[](auto t,const auto& c){return t<c.begin_ms;});
    if(it==cues.begin())return nullptr;
    const auto& cue=*(it-1);return elapsed<cue.end_ms?&cue:nullptr;
}
}
