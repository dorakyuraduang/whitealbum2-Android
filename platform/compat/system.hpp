#pragma once
#include <filesystem>
#include <functional>
#include <string_view>
namespace wr {class Process;}
namespace wa2::platform::compat {
// The shared game may prepare its own configuration at the actual profile
// read boundary. The system adapter knows no game keys or save-directory rules.
using ProfileReadHook=std::function<void(wr::Process&,const std::filesystem::path&,std::string_view,std::string_view)>;
void install_system(wr::Process&,ProfileReadHook={});
}
