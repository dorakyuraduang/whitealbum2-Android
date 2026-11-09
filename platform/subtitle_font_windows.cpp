#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "subtitle_font.hpp"
#include "file_io.hpp"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <sstream>

namespace wa2::platform {
std::vector<subtitles::GlyphMask> subtitle_glyphs(std::span<const char32_t> characters){
    if(characters.empty() || characters.size()>1536)throw std::runtime_error("Invalid subtitle character count");
    struct Context{HDC dc=CreateCompatibleDC(nullptr);HFONT font{};HGDIOBJ previous{};~Context(){if(dc && previous && previous!=HGDI_ERROR)SelectObject(dc,previous);if(font)DeleteObject(font);if(dc)DeleteDC(dc);}} context;
    if(!context.dc)throw std::runtime_error("Cannot create subtitle memory font context");
    context.font=CreateFontW(-28,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_TT_ONLY_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");
    if(!context.font || !(context.previous=SelectObject(context.dc,context.font)) || context.previous==HGDI_ERROR)throw std::runtime_error("Cannot select subtitle system font");
    MAT2 transform{};transform.eM11.value=transform.eM22.value=1;
    std::vector<subtitles::GlyphMask> out;out.reserve(characters.size());
    for(const auto cp:characters){
        const auto missing=[&]{std::ostringstream message;message<<"System subtitle font cannot render U+"<<std::hex<<unsigned(cp);throw std::runtime_error(message.str());};
        if(cp>0xffff || cp<32 || (cp>=0xd800 && cp<=0xdfff))missing();const wchar_t value=wchar_t(cp);WORD index{};
        if(GetGlyphIndicesW(context.dc,&value,1,&index,GGI_MARK_NONEXISTING_GLYPHS)==GDI_ERROR || index==0xffff)missing();
        GLYPHMETRICS metrics{};const auto bytes=GetGlyphOutlineW(context.dc,index,GGO_GRAY8_BITMAP|GGO_GLYPH_INDEX,&metrics,0,nullptr,&transform);
        if(bytes==GDI_ERROR || metrics.gmBlackBoxX>36 || metrics.gmBlackBoxY>36 || bytes>4096)missing();
        subtitles::GlyphMask glyph{int(metrics.gmptGlyphOrigin.x),-int(metrics.gmptGlyphOrigin.y),unsigned(metrics.gmBlackBoxX),unsigned(metrics.gmBlackBoxY),{}};
        const auto stride=(glyph.width+3u)&~3u;const auto expected=std::size_t(stride)*glyph.height;if(bytes<expected)missing();
        std::vector<std::uint8_t> source(bytes);if(bytes && GetGlyphOutlineW(context.dc,index,GGO_GRAY8_BITMAP|GGO_GLYPH_INDEX,&metrics,bytes,source.data(),&transform)!=bytes)missing();
        glyph.coverage.resize(std::size_t(glyph.width)*glyph.height);for(unsigned y=0;y<glyph.height;++y)for(unsigned x=0;x<glyph.width;++x){const auto alpha=source[y*stride+x];if(alpha>64)missing();glyph.coverage[y*glyph.width+x]=std::uint8_t((unsigned(alpha)*255+32)/64);}out.push_back(std::move(glyph));
    }
    return out;
}
std::shared_ptr<const subtitles::Catalogue> subtitle_file(const std::filesystem::path& path){
    auto source=open_resource_file(path);if(!source->size() || source->size()>subtitles::maximum_yaml_bytes)throw std::runtime_error("External sub.yaml must be nonempty and no larger than 1 MiB");
    std::vector<std::uint8_t> bytes(std::size_t(source->size()));source->read_at(0,bytes);return subtitles::compile_external(subtitles::parse_yaml(bytes),subtitle_glyphs);
}
}
