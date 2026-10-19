#include "text_layout.hpp"
#include "font_glyphs.hpp"
#include <algorithm>
#include <cctype>
#include <limits>
#include <map>

namespace wa2::text {
namespace {
#include "dialogue_constants.inc"
bool wide(std::uint8_t byte){return byte>=0x80 && !(byte>=0xa0 && byte<=0xdf);}
std::uint16_t code(std::string_view raw,std::size_t at){
    const auto b=std::uint8_t(raw[at]);
    if(!wide(b))return b>=0xb1 && b<=0xdd?std::uint16_t(0xff00|b):b;
    if(at+1>=raw.size())throw image::Error("truncated original text character");
    return std::uint16_t((b<<8)|std::uint8_t(raw[at+1]));
}
Colour rgb(std::uint32_t value){return {std::uint8_t(value>>16),std::uint8_t(value>>8),std::uint8_t(value)};}
bool hangs(std::string_view raw,std::size_t at){
    if(at+1>=raw.size())return false;
    const auto little=std::uint16_t(std::uint8_t(raw[at])|(std::uint8_t(raw[at+1])<<8));
    return std::find(hanging_punctuation.begin(),hanging_punctuation.end(),little)!=hanging_punctuation.end();
}
std::uint32_t number(std::string_view raw,std::size_t& at,unsigned maximum_digits=0){
    std::uint32_t result=0;unsigned count=0;
    while(at<raw.size() && raw[at]>='0' && raw[at]<='9' && (!maximum_digits || count<maximum_digits)){
        if(result>100000)throw image::Error("original text numeric control exceeds limit");
        result=result*10+unsigned(raw[at++]-'0');++count;
    }
    if(!count)throw image::Error("original text control has no numeric value");
    if(at<raw.size() && raw[at]==':')++at;
    return result;
}
}
std::string_view speaker_name(int index){
    if(index<0 || std::size_t(index)>=speaker_names.size())throw image::Error("original speaker index out of range");
    return speaker_names[std::size_t(index)];
}
int speaker_style(int index,std::string_view name){
    if(name.find(' ')!=std::string_view::npos)return 8;
    for(auto part:special_names)if(!part.empty() && name.find(part)!=std::string_view::npos)return 8;
    if(index>=0 && index<=5)return index;
    switch(index){
    case 10:case 12:case 13:case 14:case 15:case 16:case 18:case 19:case 22:case 25:case 28:case 29:case 30:
    case 32:case 33:case 34:case 38:case 46:case 50:case 51:case 52:case 53:case 54:case 55:case 56:case 57:case 98:return 6;
    case 100:return 8;case -1:return 9;default:return 7;
    }
}
Colour speaker_colour(int style){
    if(style<0 || style>10)throw image::Error("invalid native speaker colour");
    // Original default table 0x4be468 is white for every speaker category.
    return style==10?Colour{255,247,153}:Colour{};
}
Layout layout(std::string_view raw,const TextStyle& style){
    if(raw.size()>4095 || raw.find('\0')!=std::string_view::npos)throw image::Error("native text exceeds original string limit");
    if(style.size<1 || style.size>96 || style.columns<1 || style.columns>127 || style.rows<1 || style.rows>127 ||
       style.spacing<0 || style.spacing>96 || style.line_gap<0 || style.line_gap>96 ||
       style.cell!=40 || style.source_size<1 || style.source_size>40)throw image::Error("invalid native text metrics");
    Layout result;result.style=style;result.pages.emplace_back();result.letters.reserve(raw.size());
    struct Scope {char kind;int size;Colour colour,shadow;bool emphasis;std::uint32_t speed;float x;};
    std::array<Scope,15> scopes{};std::size_t depth=0;
    int size=style.size,line_height=style.size;Colour colour=style.colour,shadow=style.shadow;bool emphasis=false;
    std::uint32_t speed=10,time=0,page=0;float x=0,y=float(style.line_gap);bool wrapped=false,hanging=false;
    const auto newline=[&]{result.pages.back().end_x=x;result.pages.back().end_y=y;x=0;y+=float(line_height+style.line_gap);line_height=size;wrapped=true;};
    const auto finish_page=[&]{result.pages.back()={time,x,y};++page;result.pages.emplace_back();time=0;};
    const auto add=[&](std::uint16_t value,float px,float py,bool small=false){
        result.letters.push_back({value,px,py,small?1.f:float(size)/style.size,colour,shadow,page,time,0,small,style.outline});
    };
    for(std::size_t at=0;at<raw.size();){
        const auto b=std::uint8_t(raw[at]);
        if(b=='<' && at+1<raw.size()){
            if(depth==scopes.size())throw image::Error("original text control nesting exceeds 15");
            const auto tag=char(std::toupper(static_cast<unsigned char>(raw[at+1])));at+=2;
            scopes[depth++]={tag,size,colour,shadow,emphasis,speed,x};
            switch(tag){
            case 'A':emphasis=true;break;
            case 'B':{const auto r=number(raw,at,3),g=number(raw,at,3),blue=number(raw,at,3);
                if(r>255 || g>255 || blue>255)throw image::Error("invalid original text colour");colour={std::uint8_t(r),std::uint8_t(g),std::uint8_t(blue)};break;}
            case 'C':{const auto index=number(raw,at);if(index>=text_palette.size())throw image::Error("original text palette index out of range");colour=rgb(text_palette[index]);break;}
            case 'D':number(raw,at);break;
            case 'F':size=int(number(raw,at));if(size<1 || size>96)throw image::Error("invalid original text font size");line_height=std::max(line_height,size);break;
            case 'K':{const auto r=number(raw,at),g=number(raw,at),blue=number(raw,at);
                if(r>255 || g>255 || blue>255)throw image::Error("invalid original text shadow colour");shadow={std::uint8_t(r),std::uint8_t(g),std::uint8_t(blue)};break;}
            case 'R':break;
            case 'S':{static constexpr std::array<std::uint32_t,11> speeds{100,80,60,40,20,10,6,4,2,1,0};
                const auto index=number(raw,at);if(index>=speeds.size())throw image::Error("invalid original text speed");speed=speeds[index];break;}
            case 'W':{const auto wait=number(raw,at);if(wait>100000)throw image::Error("original text wait exceeds limit");time+=wait*10;break;}
            default:throw image::Error("unsupported original text tag "+std::string(1,tag));
            }
            continue;
        }
        if(b=='>'){
            if(!depth)throw image::Error("unmatched original text closing tag");
            const auto saved=scopes[--depth];size=saved.size;colour=saved.colour;shadow=saved.shadow;emphasis=saved.emphasis;speed=saved.speed;
            line_height=std::max(line_height,size);++at;continue;
        }
        if(b=='|' && depth && scopes[depth-1].kind=='R'){
            const auto end=raw.find('>',at+1);if(end==std::string_view::npos)throw image::Error("unterminated original ruby text");
            const auto ruby=raw.substr(at+1,end-at-1);if(ruby.empty() || ruby.size()>63)throw image::Error("invalid original ruby text");
            std::size_t count=0;for(std::size_t i=0;i<ruby.size();){const auto unit=code(ruby,i);(void)unit;i+=wide(std::uint8_t(ruby[i]))?2:1;++count;}
            const auto start=scopes[depth-1].x;const auto gap=std::max(0.f,(x-start-float(count*16))/float(count));
            float rx=start+gap/2;
            for(std::size_t i=0;i<ruby.size();){add(code(ruby,i),rx,y-13,true);rx+=16+gap;i+=wide(std::uint8_t(ruby[i]))?2:1;}
            at=end;continue;
        }
        bool escaped=false;std::uint16_t glyph{};std::size_t length=1;
        if(b=='\\'){
            if(at+1==raw.size())throw image::Error("truncated original text escape");
            const auto next=raw[at+1];at+=2;
            if(next=='n'){if(!wrapped)newline();wrapped=false;continue;}
            if(next=='k'){finish_page();continue;}
            if(std::string_view("<>\\^|~").find(next)==std::string_view::npos)throw image::Error("unsupported original text escape");
            glyph=std::uint8_t(next);escaped=true;
        }else if(b=='\n'){++at;if(!wrapped)newline();wrapped=false;continue;}
        if(!escaped){glyph=b=='^'?' ':b=='~'?',':b=='`'?'\'':code(raw,at);length=wide(b)?2:1;at+=length;}
        const auto gy=y+float((style.size-size)/2);add(glyph,x,gy);
        if(emphasis && length==2)add(0x8145,x+float(size/2),y-13,true);
        x+=float(length==2?size+style.spacing:size/2+style.spacing/2);time+=speed;wrapped=false;
        const auto limit=(style.spacing+style.size)*style.columns-size-style.spacing+1;
        if(x>=float(limit) && !(depth && scopes[depth-1].kind=='R')){
            if(hanging){
                const bool space=at+1<raw.size() && std::uint8_t(raw[at])==0x81 && std::uint8_t(raw[at+1])==0x40;
                if(!space){newline();hanging=false;}
            }else if(hangs(raw,at))hanging=true;else newline();
        }
        const auto height_limit=(style.line_gap+style.size)*style.rows-size-style.line_gap+1;
        if(y-style.line_gap>=float(height_limit) && at<raw.size())throw image::Error("original text exceeds configured page rows");
    }
    if(depth)throw image::Error("unclosed original text control");
    auto& last=result.pages.back();last.duration=time;last.end_x=x;last.end_y=y;
    return result;
}
const FontBook::Atlas& FontBook::atlas(unsigned index){
    if(index>=atlases_.size())throw image::Error("invalid original font bank");
    if(!atlases_[index]){
        const auto member=archive_.find_first(font_names[index]);if(member==resource::Archive::missing)throw image::Error("missing original font atlas");
        auto value=std::make_unique<Atlas>();value->source=archive_.load(member,16u*1024*1024);value->layout=image::inspect_game_tga(value->source);
        const auto cell=index<2?40u:24u;
        if(value->layout.bits!=8 || value->layout.width!=cell*80 || value->layout.height<cell*(glyph_count(true)/80+1))throw image::Error("unexpected original font atlas dimensions");
        atlases_[index]=std::move(value);
    }
    return *atlases_[index];
}
std::size_t FontBook::source_bytes()const noexcept {std::size_t result=0;for(const auto& item:atlases_)if(item)result+=item->source.size();return result;}
PreparedText FontBook::prepare(Layout layout){
    PreparedText result;result.layout_=std::move(layout);if(result.layout_.letters.empty())return result;
    std::map<std::pair<std::uint16_t,bool>,unsigned> slots;
    for(auto& item:result.layout_.letters){const auto [where,inserted]=slots.emplace(std::pair(item.code,item.small),unsigned(slots.size()));item.atlas_slot=where->second;}
    // Each original character occupies two small tiles in this message's atlas.
    // This bounds texture memory by the current text, not every visited glyph.
    constexpr unsigned columns=16,cell=40;
    auto pixels=std::make_shared<image::Raster>();pixels->width=columns*cell;pixels->height=unsigned((slots.size()*2+columns-1)/columns)*cell;
    pixels->rgba.resize(std::size_t(pixels->width)*pixels->height*4);
    for(const auto& [key,slot]:slots)for(unsigned layer=0;layer<2;++layer){
        const auto index=unsigned(key.second?2:0)+layer;const auto& source=atlas(index);const auto pitch=key.second?24u:40u;
        const auto box=glyph_box(key.first,true,pitch,0,pitch);
        const auto decoded=image::decode_game_tga_region(source.source,{box.x,box.y,pitch,pitch});
        const auto tile=slot*2+layer,ox=tile%columns*cell,oy=tile/columns*cell;
        for(unsigned y=0;y<pitch;++y)for(unsigned x=0;x<pitch;++x){
            const auto from=(std::size_t(y)*pitch+x)*4,to=((std::size_t(oy+y)*pixels->width)+ox+x)*4;
            // D3D A8 font uploads select palette blue as coverage. The original
            // font palette is grayscale; its reserved alpha bytes are ignored.
            pixels->rgba[to]=pixels->rgba[to+1]=pixels->rgba[to+2]=255;pixels->rgba[to+3]=decoded.rgba[from+2];
        }
    }
    result.image_=std::move(pixels);return result;
}
void PreparedText::append(std::vector<image::Sprite>& out,std::uint32_t page,std::uint32_t budget,float x,float y,std::uint8_t opacity)const {
    if(!image_ || page>=layout_.pages.size() || !opacity)return;
    for(unsigned layer=1;;--layer){
        for(const auto& item:layout_.letters){
            if(item.page!=page || budget<=item.time || (layer && !item.outline))continue;
            const auto fade=std::min<std::uint32_t>(255,(budget-item.time)*16/10);
            const auto alpha=std::uint8_t(fade*opacity/255);if(!alpha)continue;
            const auto tile=item.atlas_slot*2+layer;const auto pitch=item.small?24:40;
            const auto source_size=item.small?16:layout_.style.source_size;
            const auto inset=layer?0:(pitch-source_size)/2;
            const auto extent=layer?pitch:source_size;
            const auto offset=layer?0.f:(pitch-source_size)/2.f*item.scale;
            const auto colour=layer?item.shadow:item.colour;
            out.push_back({image_,x+item.x+offset,y+item.y+offset,extent*item.scale,extent*item.scale,
                float(tile%16*40+inset),float(tile/16*40+inset),float(extent),float(extent),colour.r,colour.g,colour.b,alpha});
        }
        if(!layer)break;
    }
}
}
