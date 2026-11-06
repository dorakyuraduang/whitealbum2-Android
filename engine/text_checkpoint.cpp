#include "text_layout.hpp"
#include "checkpoint_codec.hpp"
#include <bit>
#include <cmath>

namespace wa2::text {
namespace {
void validate(const Layout& value){
    layout({},value.style); // Applies the same font/metric limits as parsing.
    if(value.pages.size()>4096 || value.letters.size()>16384)throw image::Error("Saved text layout exceeds limits");
    const auto coordinate=[](float v){return std::isfinite(v) && std::abs(v)<=1048576;};
    for(const auto& page:value.pages)if(!coordinate(page.end_x) || !coordinate(page.end_y))throw image::Error("Invalid saved text page position");
    for(const auto& letter:value.letters)if(letter.page>=value.pages.size() || letter.time>value.pages[letter.page].duration ||
        !coordinate(letter.x) || !coordinate(letter.y) || !std::isfinite(letter.scale) || letter.scale<=0 || letter.scale>96)
        throw image::Error("Invalid saved text letter");
}
std::uint32_t rgb(Colour c){return unsigned(c.r)<<16|unsigned(c.g)<<8|unsigned(c.b);}
Colour colour(checkpoint::Reader& in){const auto v=in.u32();if(v>0xffffff)throw image::Error("Invalid saved text colour");return {std::uint8_t(v>>16),std::uint8_t(v>>8),std::uint8_t(v)};}
void real(checkpoint::Writer& out,float v){out.u32(std::bit_cast<std::uint32_t>(v));}
float real(checkpoint::Reader& in){return std::bit_cast<float>(in.u32());}
}
std::vector<std::uint8_t> checkpoint_layout(const Layout& value){
    validate(value);checkpoint::Writer out;const auto& style=value.style;
    for(auto v:{style.size,style.columns,style.rows,style.spacing,style.line_gap,style.source_size,style.cell})out.i32(v);
    out.u32(rgb(style.colour));out.u32(rgb(style.shadow));out.boolean(style.outline);
    out.u32(std::uint32_t(value.pages.size()));for(const auto& page:value.pages){out.u32(page.duration);real(out,page.end_x);real(out,page.end_y);}
    out.u32(std::uint32_t(value.letters.size()));for(const auto& letter:value.letters){out.u32(letter.code);real(out,letter.x);real(out,letter.y);real(out,letter.scale);
        out.u32(rgb(letter.colour));out.u32(rgb(letter.shadow));out.u32(letter.page);out.u32(letter.time);out.boolean(letter.small);out.boolean(letter.outline);}
    return out.seal("LSTX");
}
Layout restore_layout(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("LSTX",bytes);Layout value;auto& s=value.style;
    s.size=in.i32();s.columns=in.i32();s.rows=in.i32();s.spacing=in.i32();s.line_gap=in.i32();s.source_size=in.i32();s.cell=in.i32();s.colour=colour(in);s.shadow=colour(in);s.outline=in.boolean();
    const auto pages=in.u32();if(pages>4096)throw image::Error("Saved text page count exceeds limit");value.pages.reserve(pages);
    for(unsigned n=0;n<pages;++n){TextPage page;page.duration=in.u32();page.end_x=real(in);page.end_y=real(in);value.pages.push_back(page);}
    const auto letters=in.u32();if(letters>16384)throw image::Error("Saved text letter count exceeds limit");value.letters.reserve(letters);
    for(unsigned n=0;n<letters;++n){Letter letter;const auto code=in.u32();if(code>65535)throw image::Error("Saved text glyph code exceeds limit");letter.code=std::uint16_t(code);
        letter.x=real(in);letter.y=real(in);letter.scale=real(in);letter.colour=colour(in);letter.shadow=colour(in);letter.page=in.u32();letter.time=in.u32();letter.small=in.boolean();letter.outline=in.boolean();value.letters.push_back(letter);}
    in.finish();validate(value);return value;
}
}
