#include "scene_backlog.hpp"
#include "checkpoint_codec.hpp"
#include <fstream>
#include <iostream>

namespace s=wa2::script;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
void put(std::vector<std::uint8_t>& b,std::size_t at,unsigned v){for(unsigned n=0;n<4;++n)b.at(at+n)=std::uint8_t(v>>(n*8));}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};s::SceneBacklog log,restored;CHECK(log.entries().empty());restored.restore(log.checkpoint());CHECK(restored.checkpoint()==log.checkpoint());rejects([&]{log.append("orphan");});++guards;++cases;
    const s::BacklogVoice voice{2,1001,7,128};log.note_voice(voice);log.message("fixture",7,"first","name",10,false);
    CHECK(log.entries().size()==1 && log.entries()[0].text=="first" && log.entries()[0].name=="name" && log.entries()[0].speaker==2 && log.entries()[0].voices.size()==1 && log.entries()[0].voices[0]==voice);++cases;
    log.append(" second");CHECK(log.entries().size()==1 && log.entries()[0].text=="first second" && log.entries()[0].line==7 && log.entries()[0].voices.size()==1);++cases;
    const s::BacklogVoice other{3,1001,8,200};log.note_voice(other);log.append(" third");CHECK(log.entries()[0].voices==std::vector<s::BacklogVoice>({voice,other}) && log.entries()[0].speaker==2 && log.entries()[0].name=="name");log.note_voice(voice);log.append(" fourth");CHECK(log.entries()[0].voices==std::vector<s::BacklogVoice>({voice,other,voice}));++cases;
    restored.restore(log.checkpoint());restored.append(" restored");CHECK(restored.entries()[0].voices.size()==3 && restored.entries()[0].text=="first second third fourth restored");log.clear_voice();log.append(" silent");CHECK(log.entries()[0].voices.size()==3);log.note_voice(voice);++cases;
    const auto before_append=log.checkpoint();for(const auto& value:{std::string(4096,'x'),std::string("a\0b",3)}){rejects([&]{log.append(value);});CHECK(log.checkpoint()==before_append);++guards;}++cases;
    log.message("fixture",8,"no voice log","name",10,true,false);CHECK(log.entries()[0].voices.empty() && log.entries()[0].speaker==10 && log.entries()[0].novel);++cases;
    log.message("fixture",9,"voice log restored","name",10,false);CHECK(log.entries()[0].voices[0]==voice);++cases;
    log.message("fixture",-1,"choice","discarded name",10,false,true,true);CHECK(log.entries()[0].choice && log.entries()[0].name.empty() && log.entries()[0].voices[0]==voice);++cases;
    log.clear_voice();log.message("fixture",10,"narration",{},10,false);CHECK(log.entries()[0].speaker==-1 && log.entries()[0].voices.empty());++cases;
    s::BacklogEntry entry{"fixture","ten voices","name",11,2,false,false,std::vector<s::BacklogVoice>(10,voice)};log.push(entry);CHECK(log.entries()[0].voices.size()==10);++cases;
    log.append(" same voice");CHECK(log.entries()[0].voices.size()==10);log.note_voice(other);const auto full=log.checkpoint();rejects([&]{log.append("eleventh voice");});CHECK(log.checkpoint()==full);++guards;log.note_voice(voice);++cases;
    const auto valid=log.checkpoint();
    for(unsigned mode=0;mode<6;++mode){auto bad=entry;if(mode==0)bad.name=std::string(64,'x');if(mode==1)bad.text=std::string(4096,'x');if(mode==2)bad.text=std::string("a\0b",3);if(mode==3)bad.speaker=101;if(mode==4)bad.line=-2;if(mode==5)bad.voices.push_back(voice);
        rejects([&]{log.push(bad);});CHECK(log.checkpoint()==valid);++guards;}
    for(auto bad:{s::BacklogVoice{100,1,1,255},s::BacklogVoice{1,1,1,256},s::BacklogVoice{1,-1,1,255}}){rejects([&]{log.note_voice(bad);});CHECK(log.checkpoint()==valid);++guards;}
    log.clear();CHECK(log.entries().empty());log.note_voice(voice);for(unsigned n=0;n<258;++n)log.message("fixture",int(n),"line "+std::to_string(n),"name",10,false);
    CHECK(log.entries().size()==255 && log.entries().front().line==257 && log.entries().back().line==3);++cases;
    const auto saved=log.checkpoint();restored.restore(saved);CHECK(restored.checkpoint()==saved);restored.message("fixture",258,"next","name",10,false);CHECK(restored.entries()[0].voices[0]==voice && restored.entries().size()==255);++cases;
    const auto before=restored.checkpoint();auto corrupt=saved;corrupt.back()^=1;rejects([&]{restored.restore(corrupt);});CHECK(restored.checkpoint()==before);++guards;
    auto oversized=saved;put(oversized,12,256);put(oversized,oversized.size()-4,wa2::checkpoint::crc(std::span(oversized).first(oversized.size()-4)));rejects([&]{restored.restore(oversized);});CHECK(restored.checkpoint()==before);++guards;
    log.clear();log.message("fixture",0,"after reset","name",10,false);CHECK(log.entries().size()==1 && log.entries()[0].voices.empty());++cases;
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.native-backlog-state.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"entry_capacity\":255,\"maximum_entry_voices\":10,\"appended_text_keeps_row\":true,\"adjacent_voices_deduplicated\":true,\"append_failure_preserves_history\":true,\"synthetic_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS native backlog ordering, voice metadata and checkpoints.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
