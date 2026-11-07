#pragma once
#include "../game/user_command.hpp"
#include <SDL_events.h>
#include <optional>
#include <stdexcept>

namespace wa2::platform {
inline constexpr int user_command_event_prefix=0x57413200;
inline SDL_Event user_command_event(game::UserCommand command){
    if(!game::valid(command))throw std::invalid_argument("Unknown native SDL user command");SDL_Event event{};event.type=SDL_USEREVENT;event.user.code=user_command_event_prefix+int(command)+1;return event;
}
inline std::optional<game::UserCommand> user_command(const SDL_Event& event){
    if(event.type!=SDL_USEREVENT || (std::uint32_t(event.user.code)&0xffffff00u)!=unsigned(user_command_event_prefix))return {};
    const auto value=(unsigned(event.user.code)&255u)-1;if(value>=unsigned(game::UserCommand::count) || event.user.data1 || event.user.data2)throw std::invalid_argument("Invalid native SDL menu event");return game::UserCommand(value);
}
}
