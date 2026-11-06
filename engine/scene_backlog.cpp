#include "scene_backlog.hpp"
#include "checkpoint_codec.hpp"

namespace wa2::script {
namespace {
void validate(const BacklogVoice& voice){if(voice.speaker<0 || voice.speaker>99 || voice.chapter<0 || voice.chapter>9999 || voice.line<0 || voice.line>9999 || voice.volume<0 || voice.volume>255)throw Error("Invalid native backlog voice descriptor");}
void validate(const BacklogEntry& entry){
    const auto valid=[](const auto& text,std::size_t maximum){return text.size()<=maximum && text.find('\0')==std::string::npos;};
    if(!valid(entry.scene,240) || !valid(entry.text,4095) || !valid(entry.name,63) || entry.line<-1 || entry.speaker<-1 || entry.speaker>100 || entry.voices.size()>10 || (entry.choice && !entry.name.empty()))
        throw Error("Invalid native backlog entry");for(const auto& voice:entry.voices)validate(voice);
}
void voice(checkpoint::Writer& out,const BacklogVoice& value){out.i32(value.speaker);out.i32(value.chapter);out.i32(value.line);out.i32(value.volume);}
BacklogVoice voice(checkpoint::Reader& in){BacklogVoice value;value.speaker=in.i32();value.chapter=in.i32();value.line=in.i32();value.volume=in.i32();validate(value);return value;}
}
void SceneBacklog::note_voice(BacklogVoice voice){validate(voice);recent_voice_=voice;}
void SceneBacklog::push(BacklogEntry entry){validate(entry);entries_.push_front(std::move(entry));if(entries_.size()>capacity)entries_.pop_back();}
void SceneBacklog::message(std::string_view scene,std::int32_t line,std::string_view text,std::string_view name,std::int32_t speaker,bool novel,bool voice_log,bool choice){
    BacklogEntry entry{std::string(scene),std::string(text),choice?std::string{}:std::string(name),line,choice || name.empty()?-1:speaker,novel,choice,{}};
    if(voice_log && recent_voice_){entry.voices.push_back(*recent_voice_);entry.speaker=recent_voice_->speaker;}push(std::move(entry));
}
std::vector<std::uint8_t> SceneBacklog::checkpoint()const{
    checkpoint::Writer out;out.u32(std::uint32_t(entries_.size()));out.boolean(bool(recent_voice_));if(recent_voice_)voice(out,*recent_voice_);
    for(const auto& entry:entries_){out.string(entry.scene);out.string(entry.text);out.string(entry.name);out.i32(entry.line);out.i32(entry.speaker);out.boolean(entry.novel);out.boolean(entry.choice);out.u32(std::uint32_t(entry.voices.size()));for(const auto& item:entry.voices)voice(out,item);}
    return out.seal("LSBH");
}
void SceneBacklog::restore(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("LSBH",bytes);const auto count=in.u32();if(count>capacity)throw Error("Saved backlog entry count exceeds limit");
    std::optional<BacklogVoice> recent;if(in.boolean())recent=voice(in);std::deque<BacklogEntry> entries;
    for(unsigned n=0;n<count;++n){BacklogEntry entry;entry.scene=in.string(240);entry.text=in.string(4095);entry.name=in.string(63);entry.line=in.i32();entry.speaker=in.i32();entry.novel=in.boolean();entry.choice=in.boolean();const auto voices=in.u32();if(voices>10)throw Error("Saved backlog voice count exceeds limit");
        for(unsigned v=0;v<voices;++v)entry.voices.push_back(voice(in));validate(entry);entries.push_back(std::move(entry));}
    in.finish();entries_=std::move(entries);recent_voice_=recent;
}
}
