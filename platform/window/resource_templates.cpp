#include "resource_templates.hpp"
#include <bit>
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
}
