#include "native_desktop.hpp"
#include "file_io.hpp"
#include <algorithm>

namespace wa2::platform {
std::filesystem::path native_utf8_path(std::string_view value){
    if(value.empty() || value.size()>32767 || value.find('\0')!=std::string_view::npos)throw std::invalid_argument("Invalid native UTF-8 path");return std::filesystem::path(std::u8string(value.begin(),value.end()));
}
NativeDesktopStartup native_desktop_startup(std::span<const std::string_view> arguments){
    if(arguments.size()>32)throw std::invalid_argument("Too many native desktop arguments");NativeDesktopStartup result;std::vector<std::string_view> game;
    for(std::size_t n=0;n<arguments.size();++n){const auto argument=arguments[n];if(argument=="--resources"){
            if(result.resources || ++n==arguments.size())throw std::invalid_argument("Native --resources requires one directory");const auto value=arguments[n];
            result.resources=native_utf8_path(value);
        }else game.push_back(argument);}
    result.game=native_startup(game);return result;
}
std::filesystem::path native_desktop_directory(const std::filesystem::path& executable,const std::filesystem::path& working,const std::optional<std::filesystem::path>& selected){
    if(executable.empty() || working.empty() || !executable.is_absolute() || !working.is_absolute())throw std::invalid_argument("Native desktop discovery requires absolute host directories");
    const auto valid=[](const auto& path){return std::filesystem::is_directory(path) && std::filesystem::is_regular_file(path/"grp.pak") && std::filesystem::is_regular_file(path/"ck-gal.pak");};
    if(selected){if(selected->empty())throw std::invalid_argument("Empty selected native resource directory");const auto path=(selected->is_absolute()?*selected:working/ *selected).lexically_normal();
        if(!valid(path))throw resource::NotFound("Selected native resource directory needs grp.pak and ck-gal.pak");return path;}
    for(const auto& candidate:{executable,executable/"res",working,working/"res"})if(valid(candidate))return candidate.lexically_normal();
    throw resource::NotFound("Native resources need grp.pak and ck-gal.pak beside the program or in res; use --resources <directory>");
}
resource::BankOpener native_desktop_banks(const std::filesystem::path& directory){
    if(directory.empty() || !directory.is_absolute())throw std::invalid_argument("Native resource bank directory must be absolute");
    return [directory=directory.lexically_normal()](std::string_view name)->std::shared_ptr<resource::Source>{
        if(name.empty() || name.size()>240 || name.front()=='/' || name.find("..")!=std::string_view::npos || !name.ends_with(".pak") ||
            std::any_of(name.begin(),name.end(),[](unsigned char c){return !((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.' || c=='/');}))throw resource::Error("Invalid native resource bank name");
        try{return open_resource_file(directory/native_utf8_path(name));}catch(const resource::NotFound&){return {};}
    };
}
}
