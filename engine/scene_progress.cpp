#include "scene_progress.hpp"

namespace wa2::script {
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
