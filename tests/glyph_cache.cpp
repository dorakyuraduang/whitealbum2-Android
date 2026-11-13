#include "../engine/glyph_geometry_memo.hpp"
#include "../recompiled/native_sprites.hpp"
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("glyph cache check at ")+std::to_string(__LINE__)+": "+#x);}while(false)
using Memo=wa2::text::GlyphGeometryMemo<wa2::aot::SpriteTile>;
struct Fixture {
    std::array<std::uint8_t,256> sprite{};std::array<std::uint8_t,128> image{};std::array<std::uint8_t,4> palette{};
    std::array<std::uint32_t,195> handles{};std::array<wa2::aot::SpriteTile,1> tile{};std::uint32_t half=0x3f000000;
    Memo::Input input()const{return {sprite,image,palette,half,handles};}
    void word(unsigned at,std::uint32_t value){std::memcpy(sprite.data()+at,&value,4);}
    explicit Fixture(unsigned n){
        word(0x88,std::bit_cast<std::uint32_t>(float(n%50)*14));word(0x98,std::bit_cast<std::uint32_t>(float(n/50)*28));
        word(0xbc,std::bit_cast<std::uint32_t>(float(n%80)*40));word(0xc4,std::bit_cast<std::uint32_t>(float(n/80)*40));word(0x63,0xffffffff);word(0x48,0xffff);
        for(unsigned k=0;k<handles.size();++k)handles[k]=k+100;tile[0].texture=handles[n%handles.size()];
        for(unsigned k=0;k<4;++k)tile[0].vertices[k]={float(n),float(k),0,1,n,0,float(k)*.25f,0};
    }
};
int main(int argc,char** argv){try{
    CHECK(argc==2);static_assert(Memo::MaximumHeapBytes<=2*1024*1024);
    Memo memo;std::vector<Fixture> page;for(unsigned n=0;n<400;++n)page.emplace_back(n);
    unsigned last_hits{},exact{};
    for(unsigned pass=0;pass<6;++pass){unsigned hits{};
        for(auto& f:page){if(const auto found=memo.find(f.input())){CHECK(found->size()==1 && std::memcmp(found->data(),f.tile.data(),sizeof(f.tile))==0);++hits;++exact;}
            else memo.remember(f.input(),f.tile);}
        if(!pass)CHECK(!hits);last_hits=hits;
    }
    CHECK(last_hits>=350);
    // Freshly admit one exact key, then invalidate every dependency family.
    auto f=Fixture(777);memo.remember(f.input(),f.tile);memo.remember(f.input(),f.tile);CHECK(memo.find(f.input()));unsigned guards{};
    for(unsigned kind=0;kind<5;++kind){auto changed=f;
        if(kind==0)changed.sprite[17]^=1;if(kind==1)changed.image[73]^=1;if(kind==2)changed.palette[2]^=1;
        if(kind==3)changed.half^=1;if(kind==4)changed.handles.back()^=1;
        CHECK(!memo.find(changed.input()));CHECK(memo.find(f.input()));++guards;
    }
    auto changed=f;changed.sprite[0x88]^=1;memo.remember(changed.input(),changed.tile);CHECK(!memo.find(changed.input()));
    memo.remember(changed.input(),changed.tile);CHECK(memo.find(changed.input()));++guards;
    std::ofstream report(argv[1]);report<<"{\"page_glyphs\":400,\"passes\":6,\"last_pass_hits\":"<<last_hits<<",\"exact_tile_checks\":"<<exact<<",\"guards\":"<<guards
        <<",\"slots\":"<<Memo::Slots<<",\"ways\":"<<Memo::Ways<<",\"maximum_heap_bytes\":"<<Memo::MaximumHeapBytes<<",\"two_use_admission\":true,\"all_key_bytes_checked\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(report);
    std::cout<<"PASS bounded associative glyph cache, full keys and stable-page reuse.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
