#include "scene_movie.hpp"

namespace wa2::script{
SceneMovie::SceneMovie(Factory factory,std::function<void()> before_play,unsigned file_suffix)
    :factory_(std::move(factory)),before_play_(std::move(before_play)),suffix_(file_suffix){
    if(!factory_ || suffix_>2)throw Error("invalid native movie factory or file suffix");
}
std::optional<CommandResult> SceneMovie::dispatch(Command& command){
    if(command.index!=193)return {};
    if(command.arguments.size()!=2)throw Error("invalid SetMovie parameter count");
    if(!command.continuation){
        const auto& stem=command.arguments[0].text();
        if(stem.empty() || stem.size()>120)throw Error("invalid native movie filename");
        for(const auto c:stem)if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-'))
            throw Error("invalid native movie filename");
        if(playback_)playback_->stop();playback_.reset();
        if(before_play_)before_play_();
        playback_=factory_(stem+char('0'+suffix_)+".pak",command.arguments[1].integer());
        if(!playback_)throw Error("native movie factory returned no playback");command.continuation=1;
    }
    if(!playback_)throw Error("SetMovie resumed without a player");
    const bool done=playback_->finished();if(done)playback_.reset();
    return CommandResult{done,true,Value{}};
}
}
