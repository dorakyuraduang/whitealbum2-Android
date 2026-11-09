#include "external_subtitles.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <map>
#include <set>
#include <stdexcept>

namespace wa2::subtitles {
namespace {
[[noreturn]]void fail(std::size_t line,std::string_view message){throw std::runtime_error("sub.yaml line "+std::to_string(line)+": "+std::string(message));}
std::u32string utf8(std::string_view text,std::size_t line){
    std::u32string out;
    for(std::size_t n=0;n<text.size();){
        const auto first=std::uint8_t(text[n++]);char32_t cp{};unsigned extra{};
        if(first<0x80)cp=first;
        else if(first>=0xc2 && first<=0xdf){cp=first&31;extra=1;}
        else if(first>=0xe0 && first<=0xef){cp=first&15;extra=2;}
        else if(first>=0xf0 && first<=0xf4){cp=first&7;extra=3;}
        else fail(line,"invalid UTF-8 encoding");
        if(extra>text.size()-n)fail(line,"truncated UTF-8 character");
        for(unsigned k=0;k<extra;++k){const auto c=std::uint8_t(text[n++]);if((c&0xc0)!=0x80)fail(line,"invalid UTF-8 continuation");cp=(cp<<6)|(c&63);}
        if((extra==1 && cp<0x80) || (extra==2 && cp<0x800) || (extra==3 && cp<0x10000) || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))fail(line,"invalid UTF-8 scalar");
        out.push_back(cp);
    }
    return out;
}
std::string_view trim(std::string_view value){while(!value.empty() && (value.front()==' ' || value.front()=='\t'))value.remove_prefix(1);while(!value.empty() && (value.back()==' ' || value.back()=='\t'))value.remove_suffix(1);return value;}
std::uint32_t number(std::string_view text,std::size_t line){
    std::uint32_t out{};if(text.empty() || text.size()>10 || std::any_of(text.begin(),text.end(),[](auto c){return c<'0'||c>'9';}))fail(line,"expected an unsigned decimal integer");
    const auto result=std::from_chars(text.data(),text.data()+text.size(),out);if(result.ec!=std::errc{} || result.ptr!=text.data()+text.size())fail(line,"integer exceeds 32-bit range");return out;
}
}
Document parse_yaml(std::span<const std::uint8_t> bytes){
    if(bytes.empty() || bytes.size()>maximum_yaml_bytes)throw std::runtime_error("sub.yaml must be nonempty and no larger than 1 MiB");
    std::string_view source(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    if(source.starts_with("\xef\xbb\xbf"))source.remove_prefix(3);
    Document out;std::set<char32_t> charset;std::set<std::uint32_t> keys;
    unsigned groups{},group{};TextTrack track;TextCue cue;unsigned track_fields{},cue_fields{};std::uint32_t scene{},id{};std::size_t line{},track_line{},cue_line{};
    const auto finish_cue=[&]{
        if(!cue_fields)return;
        if(cue_fields!=7)fail(cue_line,"cue needs begin, end and text");
        if(cue.begin>=cue.end || (!track.cues.empty() && cue.begin<track.cues.back().end))fail(cue_line,"invalid or overlapping time interval");
        if(++out.cue_count>8192 || cue.text.size()>131072-out.text_count)fail(cue_line,"subtitle cue/text limit exceeded");
        out.text_count+=cue.text.size();track.cues.push_back(std::move(cue));cue={};cue_fields=0;
    };
    const auto finish_track=[&]{
        finish_cue();if(!track_fields)return;
        if(track_fields!=(group==1?5u:7u) || track.cues.empty())fail(track_line,"track needs audio id and nonempty content");
        if(id>65535 || (group==2 && (scene==0 || scene>65535)))fail(track_line,"scene/audio id exceeds supported range");
        track.key=(scene<<16)|id;if(!keys.insert(track.key).second)fail(track_line,"duplicate scene/audio identity");
        if(out.tracks.size()>=1024)fail(track_line,"subtitle track limit exceeded");
        out.tracks.push_back(std::move(track));track={};track_fields=0;scene=id=0;
    };
    for(std::size_t at=0;at<source.size();){
        auto end=source.find('\n',at);if(end==source.npos)end=source.size();auto row=source.substr(at,end-at);at=end==source.size()?end:end+1;++line;
        if(row.ends_with('\r'))row.remove_suffix(1);
        const auto decoded=utf8(row,line);for(const auto cp:decoded)if(cp==0 || (cp<32 && cp!='\t'))fail(line,"control character in subtitle file");
        const auto plain=trim(row);if(plain.empty() || plain.starts_with('#'))continue;
        if(row=="soundEffect:" || row=="voice:"){
            finish_track();group=row=="soundEffect:"?1:2;if(groups&group)fail(line,"duplicate root group");groups|=group;continue;
        }
        if(row.starts_with("    - ")){
            finish_track();if(!group)fail(line,"audio track outside soundEffect/voice");
            const auto key=group==1?"    - id: ":"    - scene: ";if(!row.starts_with(key))fail(line,"unexpected track field or indentation");
            const auto n=number(row.substr(std::string_view(key).size()),line);if(n>65535 || (group==2 && !n))fail(line,"invalid scene/audio id");
            if(group==1){id=n;scene=0;track_fields=1;}else{scene=n;track_fields=2;}track_line=line;continue;
        }
        if(row.starts_with("      id: ")){
            if(group!=2 || !track_fields || (track_fields&1))fail(line,"unexpected or duplicate voice id");
            id=number(row.substr(10),line);if(id>65535)fail(line,"audio id exceeds 65535");track_fields|=1;continue;
        }
        if(row=="      content:"){
            if(!track_fields || (track_fields&4))fail(line,"unexpected or duplicate content");track_fields|=4;continue;
        }
        if(row.starts_with("          - begin: ")){
            if(!(track_fields&4))fail(line,"cue outside content");finish_cue();cue.begin=number(row.substr(19),line);cue_fields=1;cue_line=line;continue;
        }
        if(row.starts_with("            end: ")){
            if(!cue_fields || (cue_fields&2))fail(line,"unexpected or duplicate cue end");cue.end=number(row.substr(17),line);cue_fields|=2;continue;
        }
        if(row.starts_with("            text: ")){
            if(!cue_fields || (cue_fields&4))fail(line,"unexpected or duplicate cue text");const auto text=trim(row.substr(18));
            if(text.empty() || std::string_view("!&*{}[]|>'\"%@`").find(text.front())!=std::string_view::npos)fail(line,"text must be a literal plain string");
            cue.text=utf8(text,line);if(cue.text.empty() || cue.text.size()>512)fail(line,"text exceeds 512 characters");
            for(auto cp:cue.text){if(cp<32 || (cp>=127 && cp<160) || cp==0xfeff)fail(line,"control character in subtitle text");charset.insert(cp);}
            if(charset.size()>1536)fail(line,"subtitle font exceeds 1536 unique characters");cue_fields|=4;continue;
        }
        fail(line,"unsupported field, YAML syntax or indentation; only soundEffect and voice are supported");
    }
    finish_track();if(groups!=3 || out.tracks.empty())fail(line,"both soundEffect and voice groups and at least one track are required");
    std::sort(out.tracks.begin(),out.tracks.end(),[](const auto& a,const auto& b){return a.key<b.key;});out.characters.assign(charset.begin(),charset.end());return out;
}
Atlas compose_atlas(std::span<const GlyphMask> glyphs){
    if(glyphs.empty() || glyphs.size()>1536)throw std::runtime_error("Invalid external subtitle glyph count");
    int left{},top{},right{},bottom{};
    for(const auto& glyph:glyphs){
        if(glyph.left < -40 || glyph.left>40 || glyph.top < -40 || glyph.top>40 || glyph.width>36 || glyph.height>36 || glyph.coverage.size()!=std::size_t(glyph.width)*glyph.height)throw std::runtime_error("External subtitle glyph exceeds atlas bounds");
        if(glyph.width && glyph.height){left=std::min(left,glyph.left-2);top=std::min(top,glyph.top-2);right=std::max(right,glyph.left+int(glyph.width)+2);bottom=std::max(bottom,glyph.top+int(glyph.height)+2);}
    }
    const auto cell=std::max(right-left,bottom-top)+2;if(cell<=2 || cell>40)throw std::runtime_error("External subtitle font metrics exceed the 40-pixel cell");
    Atlas out{{unsigned(cell),32,unsigned((glyphs.size()+31)/32),28,unsigned(1-left),unsigned(1-top)}, {}};
    out.image.width=out.spec.cell*out.spec.columns;out.image.height=out.spec.cell*out.spec.rows;out.image.rgba.resize(std::size_t(out.image.width)*out.image.height*4);
    for(std::size_t n=0;n<glyphs.size();++n){
        const auto& glyph=glyphs[n];std::array<std::uint8_t,1600> fill{},stroke{};
        for(unsigned y=0;y<glyph.height;++y)for(unsigned x=0;x<glyph.width;++x){const auto dx=1-left+glyph.left+int(x),dy=1-top+glyph.top+int(y);fill[dy*cell+dx]=glyph.coverage[y*glyph.width+x];}
        for(int y=0;y<cell;++y)for(int x=0;x<cell;++x)for(int oy=-2;oy<=2;++oy)for(int ox=-2;ox<=2;++ox){if(ox*ox+oy*oy>5 || x+ox<0 || y+oy<0 || x+ox>=cell || y+oy>=cell)continue;stroke[y*cell+x]=std::max(stroke[y*cell+x],fill[(y+oy)*cell+x+ox]);}
        for(int y=0;y<cell;++y)for(int x=0;x<cell;++x){const auto a=stroke[y*cell+x],f=fill[y*cell+x];const auto rgb=a?std::uint8_t((unsigned(f)*255+a/2)/a):0;const auto at=((n/32*cell+y)*out.image.width+n%32*cell+x)*4;out.image.rgba[at]=out.image.rgba[at+1]=out.image.rgba[at+2]=rgb;out.image.rgba[at+3]=a;}
    }
    return out;
}
const Track* Catalogue::find(std::uint32_t key)const noexcept{const auto it=std::lower_bound(tracks_.begin(),tracks_.end(),key,[](const auto& t,auto k){return t.key<k;});return it!=tracks_.end() && it->key==key?&*it:nullptr;}
FontSpec Catalogue::font()const noexcept{return atlas_?atlas_->spec:font_spec();}
std::shared_ptr<const Catalogue> compile_external(const Document& document,const GlyphRasterizer& rasterizer){
    if(document.tracks.empty() || document.tracks.size()>1024 || document.characters.empty() || document.characters.size()>1536 || !document.cue_count || document.cue_count>8192 || document.text_count>131072)throw std::runtime_error("Invalid external subtitle document bounds");
    auto out=std::shared_ptr<Catalogue>(new Catalogue);std::map<char32_t,std::uint16_t> lookup;const auto embedded=characters();bool covered=true;
    for(auto cp:document.characters){const auto it=std::lower_bound(embedded.begin(),embedded.end(),cp);if(it==embedded.end() || *it!=cp){covered=false;break;}}
    if(covered){for(std::size_t n=0;n<embedded.size();++n)lookup.emplace(embedded[n],std::uint16_t(n));out->glyph_count_=embedded.size();}
    else{if(!rasterizer)throw std::runtime_error("External subtitle characters require an available subtitle font adapter");const auto masks=rasterizer(document.characters);if(masks.size()!=document.characters.size())throw std::runtime_error("External subtitle font returned an incorrect glyph count");out->atlas_=std::make_unique<Atlas>(compose_atlas(masks));for(std::size_t n=0;n<document.characters.size();++n)lookup.emplace(document.characters[n],std::uint16_t(n));out->glyph_count_=document.characters.size();}
    out->glyphs_.reserve(document.text_count);out->cues_.reserve(document.cue_count);out->tracks_.reserve(document.tracks.size());
    std::size_t total{};std::uint32_t last_key{};bool first=true;
    for(const auto& track:document.tracks){
        if(track.cues.empty() || (!first && track.key<=last_key))throw std::runtime_error("Invalid external subtitle track order");last_key=track.key;first=false;const auto start=out->cues_.size();std::uint32_t previous{};
        for(const auto& cue:track.cues){if(cue.text.empty() || cue.text.size()>512 || cue.begin<previous || cue.begin>=cue.end || cue.text.size()>document.text_count-total || out->cues_.size()>=document.cue_count)throw std::runtime_error("Invalid external subtitle cue bounds");previous=cue.end;const auto offset=out->glyphs_.size();for(auto cp:cue.text){const auto it=lookup.find(cp);if(it==lookup.end())throw std::runtime_error("Subtitle character missing from validated font");out->glyphs_.push_back(it->second);}total+=cue.text.size();out->cues_.push_back({cue.begin,cue.end,{out->glyphs_.data()+offset,cue.text.size()}});}
        out->tracks_.push_back({track.key,{out->cues_.data()+start,track.cues.size()}});
    }
    if(total!=document.text_count || out->cues_.size()!=document.cue_count)throw std::runtime_error("External subtitle counts changed after validation");return out;
}
}
