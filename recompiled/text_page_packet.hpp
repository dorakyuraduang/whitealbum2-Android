#pragma once
#include "native_sprites.hpp"
#include <algorithm>

namespace wa2::aot {
struct TextPagePacket {
    SpriteCommand command{};
    std::array<SpriteTile,4> tiles;
    unsigned size{};
    bool captured{};
};
struct TextPageObserver {
    wr::Memory* memory{};
    unsigned sp{}, returning{};
    TextPagePacket* packet{};
};
inline thread_local TextPageObserver text_page_observer{};
inline void capture_page_command(wr::Memory& memory,unsigned sp,unsigned returning,const SpriteCommand& c){
    const auto observer=text_page_observer;
    if(observer.memory!=&memory || observer.sp!=sp || observer.returning!=returning)return;
    text_page_observer={};
    if(!c.glyph || c.target_kind!=SpriteTarget::surface || c.tiles.size()>4)return;
    auto& packet=*observer.packet;
    packet.command=c;packet.command.tiles={};
    packet.size=unsigned(c.tiles.size());
    std::copy(c.tiles.begin(),c.tiles.end(),packet.tiles.begin());packet.captured=true;
}
void count_page_glyph(unsigned tiles,bool wide);
}
