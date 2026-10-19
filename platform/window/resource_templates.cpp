#include "resource_templates.hpp"
#include <winrecomp/resources.hpp>
#include <bit>
#include <limits>
#include <stdexcept>
namespace wa2::window {
namespace {
class Reader {
    std::span<const std::uint8_t> bytes_;std::size_t at_{};
public:
    explicit Reader(std::span<const std::uint8_t> bytes):bytes_(bytes){if(bytes.size()>1048576)throw std::runtime_error("window resource exceeds one MiB");}
    std::size_t position() const{return at_;}
    void position(std::size_t at){if(at>bytes_.size())throw std::runtime_error("window resource offset out of bounds");at_=at;}
    void align(unsigned n){position((at_+n-1)&~std::size_t(n-1));}
    std::uint32_t number(unsigned bytes){if(bytes>bytes_.size()-at_)throw std::runtime_error("truncated window resource");std::uint32_t value{};
        for(unsigned n=0;n<bytes;++n)value|=std::uint32_t(bytes_[at_++])<<(n*8);return value;}
    std::uint16_t word(){return std::uint16_t(number(2));}
    std::uint32_t dword(){return number(4);}
    std::array<std::int16_t,4> rectangle(){std::array<std::int16_t,4> r;for(auto& value:r)value=std::bit_cast<std::int16_t>(word());return r;}
    std::u16string text(std::uint16_t first){std::u16string result;for(auto c=first;c;c=word()){if(result.size()>=32768)throw std::runtime_error("window resource text length limit");result+=char16_t(c);}return result;}
    std::u16string text(){return text(word());}
    Name name(){const auto first=word();if(!first)return {};if(first==65535)return word();return text(first);}
    std::vector<std::uint8_t> data(std::size_t size){if(size>bytes_.size()-at_)throw std::runtime_error("truncated control creation data");std::vector<std::uint8_t> data(bytes_.begin()+at_,bytes_.begin()+at_+size);at_+=size;return data;}
};
std::vector<MenuItem> menu_items(Reader& reader,bool extended,unsigned depth,unsigned& count){
    if(depth>32)throw std::runtime_error("menu nesting limit");std::vector<MenuItem> items;
    for(;;){if(++count>8192)throw std::runtime_error("menu item count limit");MenuItem item;std::uint16_t flags{};bool popup{},last{};
        if(extended){reader.align(4);item.type=reader.dword();item.state=reader.dword();item.id=reader.dword();flags=reader.word();popup=flags&1;last=flags&0x80;item.text=reader.text();reader.align(4);if(popup)item.help=reader.dword();}
        else{flags=reader.word();popup=flags&0x10;last=flags&0x80;if(!popup)item.id=reader.word();item.text=reader.text();
            item.type=flags&~(0x80u|0x10u|3u|8u);item.state=flags&(3u|8u);}
        if(popup)item.children=menu_items(reader,extended,depth+1,count);items.push_back(std::move(item));if(last)break;
    }return items;
}
}
DialogTemplate parse_dialog(std::span<const std::uint8_t> bytes){
    Reader r(bytes);DialogTemplate dialog;const auto first=r.word(),signature=r.word();dialog.extended=first==1 && signature==65535;r.position(0);unsigned count{};
    if(dialog.extended){r.word();r.word();dialog.help=r.dword();dialog.extended_style=r.dword();dialog.style=r.dword();count=r.word();}
    else{dialog.style=r.dword();dialog.extended_style=r.dword();count=r.word();}
    if(count>4096)throw std::runtime_error("dialog control count limit");dialog.bounds=r.rectangle();dialog.menu=r.name();dialog.type=r.name();dialog.title=r.text();
    if(dialog.style&0x40){Font font;font.points=r.word();if(dialog.extended){font.weight=r.word();font.italic=std::uint8_t(r.number(1));font.charset=std::uint8_t(r.number(1));}font.face=r.text();dialog.font=std::move(font);}
    dialog.controls.reserve(count);
    for(unsigned n=0;n<count;++n){r.align(4);Control control;
        if(dialog.extended){control.help=r.dword();control.extended_style=r.dword();control.style=r.dword();control.bounds=r.rectangle();control.id=r.dword();}
        else{control.style=r.dword();control.extended_style=r.dword();control.bounds=r.rectangle();control.id=r.word();}
        control.type=r.name();control.title=r.name();const auto extra=r.word();control.creation_data=r.data(extra);dialog.controls.push_back(std::move(control));
    }return dialog;
}
MenuTemplate parse_menu(std::span<const std::uint8_t> bytes){
    Reader r(bytes);const auto version=r.word(),offset=r.word();if(version>1)throw std::runtime_error("unsupported menu resource version");
    MenuTemplate menu;menu.extended=version==1;if(menu.extended)menu.help=r.dword();if(offset<(menu.extended?4:0))throw std::runtime_error("invalid menu header offset");r.position(4+offset);
    unsigned count{};menu.items=menu_items(r,menu.extended,0,count);return menu;
}
IconEntry select_icon(std::span<const std::uint8_t> bytes,unsigned desired){
    Reader r(bytes);if(r.word()!=0 || r.word()!=1)throw std::runtime_error("invalid window icon group");
    const auto count=r.word();if(!count || count>256 || bytes.size()!=6+14u*count || !desired || desired>256)throw std::runtime_error("invalid window icon directory size");
    IconEntry best;unsigned best_distance=std::numeric_limits<unsigned>::max();
    for(unsigned n=0;n<count;++n){IconEntry entry;entry.width=std::uint16_t(r.number(1));entry.height=std::uint16_t(r.number(1));if(!entry.width)entry.width=256;if(!entry.height)entry.height=256;
        r.number(1);if(r.number(1)!=0)throw std::runtime_error("invalid window icon reserved byte");entry.planes=r.word();entry.bits=r.word();entry.bytes=r.dword();entry.id=r.word();
        if(!entry.id || !entry.bytes || entry.bytes>1048576)throw std::runtime_error("invalid window icon payload size");
        const auto distance=(entry.width>desired?entry.width-desired:desired-entry.width)+(entry.height>desired?entry.height-desired:desired-entry.height);
        if(distance<best_distance || (distance==best_distance && entry.bits>best.bits)){best=entry;best_distance=distance;}
    }return best;
}
IconResource parse_icon_bitmap(std::span<const std::uint8_t> bytes,unsigned width,unsigned height){
    if(!width || width>256 || !height || height>256 || bytes.size()>1048576)throw std::runtime_error("invalid window icon dimensions");
    Reader r(bytes);const auto header=r.dword();if(header!=40 && header!=108 && header!=124)throw std::runtime_error("unsupported window icon bitmap header");
    if(r.dword()!=width || std::bit_cast<std::int32_t>(r.dword())!=int(height*2))throw std::runtime_error("window icon bitmap dimensions disagree");
    if(r.word()!=1 || r.word()!=32 || r.dword()!=0)throw std::runtime_error("unsupported window icon bitmap format (requires uncompressed BGRA32)");
    r.dword();r.dword();r.dword();if(r.dword()!=0)throw std::runtime_error("unsupported window icon palette");r.dword();
    const auto required=std::size_t(header)+width*height*4+((width+31)/32)*4*height;
    if(bytes.size()<required)throw std::runtime_error("truncated window icon pixels or transparency mask");
    return {std::uint16_t(width),std::uint16_t(height),{bytes.begin(),bytes.end()}};
}
IconResource load_icon_resource(const wr::Image& image,const Name& name){
    wr::ResourceName key;
    if(const auto* id=std::get_if<std::uint16_t>(&name))key=*id;else if(const auto* text=std::get_if<std::u16string>(&name))key=*text;else throw std::runtime_error("window icon has no resource name");
    const auto group=wr::find_resource(image,std::uint16_t(14),key);if(!group)throw std::runtime_error("window icon group resource missing");
    const auto entry=select_icon(wr::resource_bytes(image,*group));
    if(entry.planes!=1 || entry.bits!=32)throw std::runtime_error("unsupported window icon group format");
    const auto icon=wr::find_resource(image,std::uint16_t(3),entry.id,group->language);if(!icon || icon->size!=entry.bytes)throw std::runtime_error("window icon image resource missing or size differs");
    return parse_icon_bitmap(wr::resource_bytes(image,*icon),entry.width,entry.height);
}
}
