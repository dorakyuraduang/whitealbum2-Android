#include "saved_scene.hpp"
#include "checkpoint_codec.hpp"
#include <limits>
#include <stdexcept>

namespace wa2::game {
namespace {
void validate(const SavedScene& value){
    if(value.logical_us<0 || value.command>65535 || value.scene.empty() || value.scene.size()>240 || value.replay>24)throw std::runtime_error("Invalid native scene save metadata");
    for(unsigned char c:value.scene)if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-'))throw std::runtime_error("Invalid native saved scene identifier");
    for(unsigned n=0;n<value.parts.size();++n)checkpoint::Reader::open(scene_part_tags[n],value.parts[n]);
}
}
SavedScene::Bytes SavedScene::encode()const{
    validate(*this);checkpoint::Writer out;out.u64(std::uint64_t(logical_us));out.u32(random);out.u32(command);out.u64(ticks);out.u64(messages);out.u64(voices);out.string(scene);
    for(bool v:{background_changing,preserve_characters,window_visible,skip_read_only})out.boolean(v);out.u32(replay);out.u32(std::uint32_t(parts.size()));for(const auto& part:parts)out.blob(part);return out.seal("LSGS");
}
SavedScene SavedScene::decode(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("LSGS",bytes);SavedScene value;const auto clock=in.u64();if(clock>std::uint64_t(std::numeric_limits<std::int64_t>::max()))throw std::runtime_error("Invalid saved gameplay clock");value.logical_us=std::int64_t(clock);value.random=in.u32();value.command=in.u32();value.ticks=in.u64();value.messages=in.u64();value.voices=in.u64();value.scene=in.string(240);
    for(auto* v:{&value.background_changing,&value.preserve_characters,&value.window_visible,&value.skip_read_only})*v=in.boolean();value.replay=in.u32();if(in.u32()!=value.parts.size())throw std::runtime_error("Native scene save subsystem count changed");for(auto& part:value.parts){const auto data=in.blob();part.assign(data.begin(),data.end());}in.finish();validate(value);return value;
}
}
