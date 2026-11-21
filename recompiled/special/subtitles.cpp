#include "subtitles.hpp"
#include "subtitle_profile.hpp"
#include "../../platform/clock.hpp"
#include <limits>
#include <stdexcept>

namespace wa2::special {
SubtitleState::SubtitleState(std::shared_ptr<subtitles::Playback> player,std::function<std::uint64_t()> clock):player_(std::move(player)),clock_(std::move(clock)){
    if(!player_)throw std::invalid_argument("Missing SC subtitle player");
    if(!clock_)clock_=[]{return platform::Clock::counter()/1000000;};
}
void SubtitleState::returned(std::uint32_t entry,const wr::Cpu&,wr::Memory& memory,std::uint32_t args){
    if(!player_->enabled())return;
    const auto argument=[&](unsigned index){const auto at=std::uint64_t(args)+4*index;if(at>std::numeric_limits<std::uint32_t>::max())throw wr::GuestFault(wr::FaultKind::unsupported,entry,"SC subtitle argument overflow");return memory.load(std::uint32_t(at),32);};
    const auto play=[&](std::uint64_t delay){if(bgm_<=65535 && chapter_<=65535){player_->play((bgm_<<16)|chapter_,subtitles::Kind::voice,0,clock_()+delay,true);}};
    if(entry==subtitle_bgm_entry){
        const auto bgm=argument(1);if(bgm<80 || bgm>65535){bgm_=0;player_->reset();return;}
        bgm_=bgm;chapter_=memory.load(subtitle_playing_chapter,32);play(0);++starts_;return;
    }
    if(entry==subtitle_chapter_entry){
        // This interpolation helper also appears in ordinary effects. Avoid
        // guest reads and clock calls unless a drama is actually being tracked.
        if(bgm_<80)return;
        if((argument(1)&0x7fffffffu)!=0 || argument(2)!=0x42800000u)return;
        const auto chapter=memory.load(subtitle_next_chapter,32);if(chapter==chapter_)return;
        chapter_=chapter;play(subtitle_chapter_delay);++changes_;return;
    }
    if(entry==subtitle_reset_entry){bgm_=0;player_->reset();++stops_;return;}
    throw wr::GuestFault(wr::FaultKind::unsupported,entry,"Unknown SC subtitle audio event");
}
std::string SubtitleState::report()const{
    return "{\"enabled\":"+std::string(player_->enabled()?"true":"false")+",\"tracks\":"+std::to_string(player_->enabled()?player_->tracks().size():0)+",\"starts\":"+std::to_string(starts_)+",\"chapter_changes\":"+std::to_string(changes_)+",\"stops\":"+std::to_string(stops_)+",\"main_story_tracks_used\":false,\"resources_external\":true}";
}
}
