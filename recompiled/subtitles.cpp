#include "subtitles.hpp"
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>

namespace wa2::aot {
namespace {
struct Bindings {std::mutex mutex;std::map<const wr::Memory*,std::weak_ptr<subtitles::Playback>> players;};
Bindings& subtitle_bindings(){static Bindings value;return value;}
}
std::shared_ptr<subtitles::Playback> bind_source_subtitles(wr::Memory& memory){
    auto player=std::make_shared<subtitles::Playback>();
    auto& registry=subtitle_bindings();const std::lock_guard lock(registry.mutex);auto& bindings=registry.players;
    for(auto it=bindings.begin();it!=bindings.end();)if(it->second.expired())it=bindings.erase(it);else ++it;
    if(auto it=bindings.find(&memory);it!=bindings.end())throw std::logic_error("duplicate native subtitle binding");
    bindings[&memory]=player;return player;
}
std::shared_ptr<subtitles::Playback> find_source_subtitles(wr::Memory& memory){
    auto& registry=subtitle_bindings();const std::lock_guard lock(registry.mutex);auto& bindings=registry.players;
    const auto it=bindings.find(&memory);return it==bindings.end()?nullptr:it->second.lock();
}
void unbind_source_subtitles(wr::Memory& memory,const subtitles::Playback* player) noexcept {
    auto& registry=subtitle_bindings();const std::lock_guard lock(registry.mutex);auto& bindings=registry.players;
    const auto it=bindings.find(&memory);
    if(it!=bindings.end() && it->second.lock().get()==player)bindings.erase(it);
}
void source_subtitle_return(subtitles::Playback& playback,std::uint32_t entry,
    const wr::Cpu&,wr::Memory& memory,std::uint32_t args,std::uint64_t now){
    using subtitles::Kind;
    const auto argument=[&](unsigned n){
        const auto address=std::uint64_t(args)+n*4;
        if(address>std::numeric_limits<std::uint32_t>::max())
            throw wr::GuestFault(wr::FaultKind::unsupported,entry,"subtitle audio argument overflow");
        return memory.load(std::uint32_t(address),32);
    };
    const auto play=[&](std::uint32_t scene,std::uint32_t id,std::uint32_t slot,bool replace=false){
        if(scene<=65535 && id<=65535)playback.play(scene<<16|id,scene?Kind::voice:Kind::effect,slot,now,replace);
    };
    switch(entry){
    case 0x40f400:play(0,argument(1),argument(0));break;
    case 0x40eda0:{
        auto scene=argument(2);
        if(scene==0xffffffff){scene=memory.load(0x4be4cc,32);if(scene==0xffffffff)scene=memory.load(0xa391e0,32);}
        // Match the audio's source scene, including reused voices, rather than
        // the currently displayed story scene. Preserve the 32-bit ABI sum.
        const auto id=std::uint32_t(memory.load(0xa391ec,32)+argument(3));
        play(scene,id,argument(0));break;
    }
    case 0x406270:play(9500,argument(0)-1,0,true);break;
    case 0x40f590:playback.stop(Kind::effect,argument(0));break;
    case 0x40ec30:playback.stop(Kind::voice,argument(0));break;
    default:throw wr::GuestFault(wr::FaultKind::unsupported,entry,"unknown subtitle audio event");
    }
}
}
