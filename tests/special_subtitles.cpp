#include "../engine/special_subtitles.hpp"
#include "../platform/special_subtitle_files.hpp"
#include "../recompiled/special/runtime.hpp"
#include "subtitle_profile.hpp"
#include <winrecomp/win32_state.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>

namespace s=wa2::subtitles;namespace sc=wa2::special;namespace a=wa2::aot;
unsigned checks{},guards{},returns{},main_cues{};
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("check failed: " #x);}while(false)
template<class F>void rejects(F f){bool bad{};try{f();}catch(const std::exception&){bad=true;}CHECK(bad);++guards;}
void word(std::vector<std::uint8_t>& out,std::uint32_t value,unsigned bytes=4){for(unsigned n=0;n<bytes;++n)out.push_back(std::uint8_t(value>>(8*n)));}
void text(std::vector<std::uint8_t>& out,std::initializer_list<unsigned> ids){word(out,unsigned(ids.size()));for(auto id:ids)word(out,id,2);}
std::vector<std::uint8_t> data(){
    std::vector<std::uint8_t> out;word(out,0x376e7378);word(out,2);
    word(out,80u<<16);word(out,2);
    word(out,0);word(out,1000);text(out,{1,2});text(out,{3});
    word(out,1000);word(out,2000);text(out,{5});text(out,{});
    word(out,(80u<<16)|1);word(out,1);word(out,0);word(out,100);text(out,{6});text(out,{});
    word(out,1);word(out,80u<<16);word(out,1);word(out,500);word(out,1500);text(out,{4});return out;
}
std::vector<std::uint8_t> font(){std::vector<std::uint8_t> out(18+960*32*4);out[2]=2;out[12]=0xc0;out[13]=3;out[14]=32;out[16]=32;out[17]=0x28;for(std::size_t n=18;n<out.size();n+=4){out[n]=33;out[n+1]=22;out[n+2]=11;out[n+3]=44;}return out;}
void same(const wr::Cpu& x,const wr::Cpu& y){CHECK(x.r==y.r && x.eip==y.eip && x.flags==y.flags && x.defined_flags==y.defined_flags && x.fp.bytes==y.fp.bytes && x.selectors==y.selectors && x.fs_base==y.fs_base && x.gs_base==y.gs_base && x.fs_valid==y.fs_valid && x.gs_valid==y.gs_valid);}
void source_returns(const std::shared_ptr<const s::Catalogue>& catalogue,bool linear){
    wr::Memory memory;if(linear)CHECK(memory.enable_linear_memory());memory.map(0x10000000,4096,wr::Memory::Read|wr::Memory::Write);memory.map(0xa6f000,4096,wr::Memory::Read|wr::Memory::Write);memory.map(0x4bf000,4096,wr::Memory::Read|wr::Memory::Write);
    constexpr std::uint32_t args=0x10000104;std::uint64_t now=1000,clocks{};auto player=std::make_shared<s::Playback>();player->catalogue(catalogue);
    sc::SubtitleState state(player,[&]{++clocks;return now;});wr::Cpu cpu;const auto set=[&](unsigned index,unsigned value){memory.store(args+4*index,value,32);};
    const auto callback=[](std::uint32_t entry,const wr::Cpu& cpu,wr::Memory& ram,std::uint32_t args,void* state){static_cast<sc::SubtitleState*>(state)->returned(entry,cpu,ram,args);};
    memory.store(sc::subtitle_playing_chapter,0,32);memory.store(sc::subtitle_next_chapter,1,32);set(1,80);
    for(auto pc:{0x460d0bu,0x460f05u,0x44b7b0u,0x406a56u}){
        player->reset();set(1,80);state.returned(sc::subtitle_bgm_entry,cpu,memory,args);CHECK(player->current(now));
        const auto location=sc::locate(pc);CHECK(location.function);
        const auto entry=location.function->entry;CHECK(entry==sc::subtitle_bgm_entry || entry==sc::subtitle_chapter_entry || entry==sc::subtitle_reset_entry);
        if(entry==sc::subtitle_chapter_entry){set(1,0);set(2,0x42800000);}
        if(entry==sc::subtitle_bgm_entry)player->reset();
        cpu.eip=pc;cpu.r[wr::ESP]=args-4;memory.store(args-4,0xf0001234,32);auto original=cpu;const auto previous=clocks;
        {auto m=memory.execution_scope();std::uint64_t budget=100;location.function->body(original,memory,m,budget,true,location.resume);}
        CHECK(clocks==previous);
        {a::SubtitleEventScope events(memory,callback,&state);auto m=memory.execution_scope();std::uint64_t budget=100;location.function->body(cpu,memory,m,budget,false,location.resume);}
        same(cpu,original);CHECK(cpu.eip==0xf0001234 && cpu.r[wr::ESP]==args+(entry==sc::subtitle_bgm_entry?28u:0u));CHECK(memory.load(args-4,32)==0xf0001234);
        if(entry==sc::subtitle_bgm_entry)CHECK(player->current(now));
        else if(entry==sc::subtitle_chapter_entry){CHECK(!player->current(now+999));CHECK(player->current(now+1000)==&catalogue->find((80u<<16)|1)->cues[0]);}
        else CHECK(!player->current(now));++returns;
    }
    wr::Memory inaccessible;player->enabled(false);const auto previous=clocks;
    for(auto entry:{sc::subtitle_bgm_entry,sc::subtitle_chapter_entry,sc::subtitle_reset_entry})state.returned(entry,cpu,inaccessible,0xffffffff);
    CHECK(clocks==previous);player->enabled(true);state.returned(sc::subtitle_reset_entry,cpu,inaccessible,0xffffffff);state.returned(sc::subtitle_chapter_entry,cpu,inaccessible,0xffffffff);CHECK(clocks==previous);
    rejects([&]{state.returned(0x404040,cpu,memory,args);});rejects([&]{state.returned(sc::subtitle_bgm_entry,cpu,memory,0xfffffffc);});
    player->reset();set(1,80);state.returned(sc::subtitle_bgm_entry,cpu,memory,args);CHECK(player->current(now));
    const auto location=sc::locate(0x460d0b);cpu.eip=0x460d0b;cpu.r[wr::ESP]=0xffffffff;const auto before=clocks;
    rejects([&]{a::SubtitleEventScope events(memory,callback,&state);auto m=memory.execution_scope();std::uint64_t budget=100;location.function->body(cpu,memory,m,budget,false,location.resume);});CHECK(clocks==before && player->current(now));
}
int main(int argc,char** argv){try{
    CHECK(argc==2 || argc==3);const auto input=data(),atlas=font();auto catalogue=s::compile_special_binary(input,atlas,atlas);CHECK(catalogue->tracks().size()==2 && catalogue->glyph_count()==60);
    const auto* track=catalogue->find(80u<<16);CHECK(track && track->cues.size()==4);CHECK(track->cues[0].begin_ms==0 && track->cues[0].end_ms==500 && track->cues[0].glyphs.size()==4);CHECK(track->cues[1].glyphs.size()==6 && track->cues[1].glyphs.back()==34);CHECK(track->cues[2].glyphs.size()==3 && track->cues[3].glyphs.size()==1);
    const auto* image=catalogue->atlas();CHECK(image && image->width==960 && image->height==64 && image->rgba[0]==11 && image->rgba[1]==22 && image->rgba[2]==33 && image->rgba[3]==44);
    s::Playback player;player.catalogue(catalogue);CHECK(player.play(80u<<16,s::Kind::voice,0,1000,true));CHECK(!player.current(999) && player.current(1000)==&track->cues[0] && player.current(1500)==&track->cues[1]);CHECK(player.layout(track->cues[1]).size()==4);CHECK(!player.current(3000));player.enabled(false);CHECK(!player.play(80u<<16,s::Kind::voice,0,0,true));
    source_returns(catalogue,false);source_returns(catalogue,true);
    for(std::size_t size:{0u,4u,8u,17u})rejects([&]{s::compile_special_binary(std::span(input).first(size),atlas,atlas);});
    {auto bad=input;bad[0]=0;rejects([&]{s::compile_special_binary(bad,atlas,atlas);});bad=input;bad.push_back(0);rejects([&]{s::compile_special_binary(bad,atlas,atlas);});bad=input;bad[28]=30;rejects([&]{s::compile_special_binary(bad,atlas,atlas);});bad=input;bad[4]=255;rejects([&]{s::compile_special_binary(bad,atlas,atlas);});}
    for(auto index:{12u,13u,14u,16u}){auto bad=atlas;bad[index]=0;rejects([&]{s::compile_special_binary(input,bad,atlas);});}
    // Retain all embedded Main cue boundaries and geometry with the same engine.
    for(const auto& track:s::main_story_tracks()){s::Playback main;const auto kind=track.key<65536?s::Kind::effect:s::Kind::voice;CHECK(main.play(track.key,kind,0,1000));for(const auto& cue:track.cues){CHECK(main.current(1000+cue.begin_ms)==&cue && main.current(1000+cue.end_ms-1)==&cue);CHECK(main.layout(cue).size()==cue.glyphs.size());++main_cues;}CHECK(!main.current(1000+track.cues.back().end_ms));}
    unsigned external_tracks{},external_cues{};if(argc==3){const auto external=wa2::platform::special_subtitle_files(std::filesystem::path(argv[2]));external_tracks=unsigned(external->tracks().size());for(const auto& track:external->tracks())external_cues+=unsigned(track.cues.size());CHECK(external_tracks && external_cues);}
    std::ofstream out(argv[1]);out<<"{\"passed\":true,\"checks\":"<<checks<<",\"guards\":"<<guards<<",\"static_return_comparisons\":"<<returns<<",\"main_cue_regressions\":"<<main_cues<<",\"external_tracks\":"<<external_tracks<<",\"external_cues\":"<<external_cues<<",\"sc_dialogue_secondary_and_song_merged\":true,\"chapter_delay_checked\":true,\"disabled_skips_guest_reads\":true,\"source_ret_fault_precedes_observer\":true,\"game_launched\":false,\"screen_access\":false,\"device_gameplay_verified\":false}\n";CHECK(out);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
