// Byte-exact glyph geometry memo checks; owned synthetic inputs, no renderer.
#include "../engine/glyph_geometry_memo.hpp"
#include <fstream>
#include <iostream>
#include <string>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
using Tile=std::array<std::uint32_t,33>;
using Memo=wa2::text::GlyphGeometryMemo<Tile>;
struct Data {
    std::array<std::uint8_t,256> sprite;
    std::array<std::uint8_t,128> image;
    std::array<std::uint8_t,4> palette;
    std::uint32_t half=0x3f000000;
    std::array<std::uint32_t,257> handles;
    unsigned count=256;
    Memo::Input input()const{return {sprite,image,palette,half,std::span(handles).first(count)};}
};
int main(int argc,char** argv){try{
    CHECK(argc==2);Memo memo;Data original;
    for(unsigned n=0;n<256;++n)original.sprite[n]=std::uint8_t(n*37+5);
    for(unsigned n=0;n<128;++n)original.image[n]=std::uint8_t(n*71+7);
    for(unsigned n=0;n<4;++n)original.palette[n]=std::uint8_t(n*11+9);
    for(unsigned n=0;n<257;++n)original.handles[n]=0x80000000u+n*17;
    std::array<Tile,5> tiles;for(unsigned n=0;n<tiles.size();++n)for(unsigned k=0;k<33;++k)tiles[n][k]=n*131+k*37;
    unsigned mutations{},payloads{},declines{};
    auto remember=[&](const Memo::Input& key,std::span<const Tile> values){memo.remember(key,values);memo.remember(key,values);};
    auto same=[&](const Data& key,unsigned count){const auto found=memo.find(key.input());CHECK(found && found->size()==count);
        for(unsigned n=0;n<count;++n)CHECK((*found)[n]==tiles[n]);};
    CHECK(!memo.find(original.input()));
    memo.remember(original.input(),std::span(tiles).first(4));CHECK(!memo.find(original.input()));
    for(unsigned count=0;count<=4;++count){remember(original.input(),std::span(tiles).first(count));same(original,count);++payloads;}
    auto independent=original;remember(independent.input(),std::span(tiles).first(4));independent.sprite.fill(0);independent.image.fill(0);independent.handles.fill(0);same(original,4); // owns its key
    auto mutation=[&](Data changed){CHECK(!memo.find(changed.input()));remember(changed.input(),std::span(tiles).first(3));same(changed,3);
        if(memo.find(original.input()))same(original,4);remember(original.input(),std::span(tiles).first(4));same(original,4);++mutations;};
    for(unsigned n=0;n<256;++n){auto changed=original;changed.sprite[n]^=1;mutation(changed);}
    for(unsigned n=0;n<128;++n){auto changed=original;changed.image[n]^=1;mutation(changed);}
    for(unsigned n=0;n<4;++n){auto changed=original;changed.palette[n]^=1;mutation(changed);}
    for(unsigned n=0;n<32;++n){auto changed=original;changed.half^=1u<<n;mutation(changed);}
    for(unsigned n=0;n<256;++n){auto changed=original;changed.handles[n]^=1;mutation(changed);}
    for(unsigned count=0;count<256;++count){auto changed=original;changed.count=count;mutation(changed);}
    auto oversized=original;oversized.count=257;CHECK(!memo.find(oversized.input()));memo.remember(oversized.input(),std::span(tiles).first(4));CHECK(!memo.find(oversized.input()));same(original,4);++declines;
    auto big=original;big.sprite[0]^=0x80;memo.remember(big.input(),tiles);CHECK(!memo.find(big.input()));same(original,4);++declines;
    // Collisions/replacement are exact and bounded, even with a new character
    // and texture data on every operation. No entry aliases caller storage.
    for(unsigned n=0;n<512;++n){auto changed=original;changed.sprite[0]=std::uint8_t(n);changed.sprite[1]=std::uint8_t(n>>8);changed.handles[n&255]^=n;
        remember(changed.input(),std::span(tiles).first(n%5));same(changed,n%5);++payloads;}
    Memo single_pass;for(unsigned n=0;n<512;++n){auto changed=original;changed.sprite[0]=std::uint8_t(n);changed.sprite[1]=std::uint8_t(n>>8);
        single_pass.remember(changed.input(),std::span(tiles).first(4));CHECK(!single_pass.find(changed.input()));}
    CHECK(Memo::MaximumHeapBytes<=256*1024);
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.glyph-geometry-memo.v1\",\"key_mutations\":"<<mutations<<",\"exact_payload_cases\":"<<payloads<<",\"unchanged_declines\":"<<declines
        <<",\"maximum_heap_bytes\":"<<Memo::MaximumHeapBytes<<",\"slots\":"<<Memo::Slots<<",\"maximum_tiles\":"<<Memo::MaximumTiles<<",\"single_use_no_capture\":512,\"owns_keys_and_tiles\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out.good());
    std::cout<<"PASS glyph geometry memo: "<<mutations<<" key mutations, "<<payloads<<" exact payloads.\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
