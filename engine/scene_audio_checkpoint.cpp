#include "scene_music.hpp"
#include "scene_effects.hpp"
#include "scene_voices.hpp"
#include "checkpoint_codec.hpp"

namespace wa2::script {
namespace {
void volume_range(std::int32_t value){if(value<0 || value>255)throw Error("Invalid saved audio volume");}
template<class Slots>struct PendingAudio {
    Slots& slots;bool committed{};
    ~PendingAudio(){if(!committed)for(auto& slot:slots)if(slot.voice)slot.voice->stop();}
};
}
// These are original load/resume specifications, not PCM transport snapshots.
// Cached 0x423650/0x423ea0 restart music and looping SE/voices, omit ordinary
// speech/one-shot sounds and do not save a decoder or playback cursor.
std::vector<std::uint8_t> SceneMusic::checkpoint()const{
    checkpoint::Writer out;out.i32(song_);if(song_!=-1){const auto& slot=slots_[current_];if(slot.song!=song_ || !slot.voice)throw Error("Inconsistent native music save state");out.boolean(slot.loop);out.i32(slot.target);}return out.seal("LSMR");
}
void SceneMusic::restore(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("LSMR",bytes);const auto song=in.i32();if(song< -1 || song>=200)throw Error("Invalid saved music identity");bool loop=true;std::int32_t volume{};if(song!=-1){loop=in.boolean();volume=in.i32();volume_range(volume);}in.finish();
    std::array<MusicSlot,2> next;PendingAudio pending{next};if(song!=-1){auto& slot=next[0];slot.voice=factory_(song,loop);if(!slot.voice)throw Error("Saved music loader returned no voice");slot.voice->gain(0);slot.song=song;slot.loop=loop;slot.volume=slot.from=slot.target=volume;slot.duration=1;slot.phase=MusicPhase::fade_in;}
    for(auto& slot:slots_)if(slot.voice)slot.voice->stop();slots_=std::move(next);pending.committed=true;current_=0;song_=song;for(auto& slot:slots_)apply(slot);
}
std::vector<std::uint8_t> SceneEffects::checkpoint()const{
    checkpoint::Writer out;for(const auto& slot:slots_){const bool resume=slot.voice && slot.loop;out.boolean(resume);if(resume){out.i32(slot.sound);out.i32(slot.target);}}return out.seal("LSER");
}
void SceneEffects::restore(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("LSER",bytes);std::array<EffectSlot,16> next;PendingAudio pending{next};for(auto& slot:next)if(in.boolean()){slot.sound=in.i32();slot.target=in.i32();if(slot.sound<0)throw Error("Invalid saved sound-effect identity");volume_range(slot.target);slot.loop=true;slot.duration=-2;}in.finish();
    for(auto& slot:next)if(slot.loop){if(!prepared_factory_)throw Unsupported("Missing saved sound-effect preparation factory");slot.voice=prepared_factory_(slot.sound,true,true);if(!slot.voice)throw Error("Saved sound-effect loader returned no voice");slot.voice->gain(0);}
    stop_all();slots_=std::move(next);pending.committed=true;for(unsigned n=0;n<slots_.size();++n)if(auto& slot=slots_[n];slot.voice){slot.voice->pause(false);if(events_.started)events_.started(n,slot.sound,*slot.voice);}
}
std::vector<std::uint8_t> SceneVoices::checkpoint()const{
    checkpoint::Writer out;out.u64(played_);for(const auto& slot:slots_){const bool resume=slot.voice && slot.line.loop;out.boolean(resume);if(resume){out.i32(slot.line.speaker);out.i32(slot.line.chapter);out.i32(slot.line.line);out.i32(slot.from);}}return out.seal("LSVR");
}
void SceneVoices::restore(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("LSVR",bytes);const auto played=in.u64();std::array<Slot,4> next;PendingAudio pending{next};std::array<bool,4> resume{};
    for(unsigned n=0;n<next.size();++n)if((resume[n]=in.boolean())){auto& slot=next[n];slot.line={in.i32(),in.i32(),in.i32(),true};slot.volume=slot.from=slot.target=slot.remembered=in.i32();if(slot.line.speaker<0 || slot.line.speaker>99 || slot.line.chapter<0 || slot.line.chapter>9999 || slot.line.line<0 || slot.line.line>9999)throw Error("Invalid saved looping voice identity");volume_range(slot.volume);}in.finish();
    for(unsigned n=0;n<next.size();++n)if(resume[n]){auto& slot=next[n];if(mute_adult_ && adult_mode_ && slot.line.speaker==0)continue;slot.voice=factory_(slot.line);if(!slot.voice)throw Error("Saved looping voice loader returned no voice");slot.voice->gain(0);}
    for(auto& slot:slots_)stop(slot);slots_=std::move(next);pending.committed=true;played_=played;for(unsigned n=0;n<slots_.size();++n)if(auto& slot=slots_[n];slot.voice){apply(slot);if(events_.started)events_.started(n,slot.line,*slot.voice);}
}
}
