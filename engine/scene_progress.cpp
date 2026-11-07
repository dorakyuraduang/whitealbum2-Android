#include "scene_progress.hpp"
#include <bit>

namespace wa2::script {
void SceneProgress::event_image(unsigned bank,std::int32_t index){
    if(bank>1 || index<0 || std::uint32_t(index)>=progress::layout::gallery_bank_bytes)throw Error("Native automatic gallery index is out of range");
    if(!store_)throw Error("Missing native automatic gallery progress storage");
    store_->state().set_gallery(bank,std::uint32_t(index),1);store_->flush();
}
void SceneProgress::image_file(std::string_view name){
    if(name.empty())return;const char prefix=name.front();if(prefix!='v' && prefix!='V' && prefix!='h' && prefix!='H')return;
    if(name.size()>255 || name.find('\0')!=std::string_view::npos)throw Error("Invalid native automatic gallery filename");
    name.remove_prefix(1);bool negative=false;if(!name.empty() && name.front()=='-'){negative=true;name.remove_prefix(1);}
    // Cached 44e720 accepts a leading minus and contiguous decimal digits;
    // unlike CRT atoi it does not skip whitespace or a plus sign. Accumulation
    // wraps at 32 bits. The validated gallery index never becomes an address.
    std::uint32_t index{};for(unsigned char c:name){if(c<'0' || c>'9')break;index=index*10+unsigned(c-'0');}
    event_image(prefix=='h' || prefix=='H',std::bit_cast<std::int32_t>(negative?0-index:index));
}
std::optional<CommandResult> SceneProgress::dispatch(Command& command){
    if(command.index!=198 && command.index!=199 && command.index!=201 && command.index!=202)return {};
    if(!store_)throw Error("Missing native progress storage for script command "+std::to_string(command.index));
    const auto count=command.index==198?1u:command.index==199?2u:3u;
    if(command.arguments.size()!=count)throw Error("Invalid native progress command parameters");
    const auto integer=[&](unsigned index){return command.arguments[index].integer();};
    auto& state=store_->state();
    if(command.index==198)return CommandResult{true,false,Value(state.flag(std::uint32_t(integer(0))))};
    if(command.index==199)state.set_flag(std::uint32_t(integer(0)),integer(1));
    else{
        // V_Flag / H_Flag use group*10+difference, and store the low byte.
        // Validate the resulting index before touching either gallery bank.
        const auto index=std::int64_t(integer(0))*10+integer(1);
        if(index<0 || index>=std::int64_t(progress::layout::gallery_bank_bytes))throw Error("Native gallery flag address out of range");
        state.set_gallery(command.index==201?0u:1u,std::uint32_t(index),std::uint8_t(integer(2)));
    }
    store_->flush();return CommandResult{true,false,Value{}};
}
}
