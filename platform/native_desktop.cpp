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
LegacySettingsOpener native_desktop_legacy_settings(const std::filesystem::path& directory){
    if(directory.empty() || !directory.is_absolute())throw std::invalid_argument("Native settings source directory must be absolute");
    return [directory=directory.lexically_normal()](std::string_view name)->std::shared_ptr<resource::Source>{
        if(!legacy_settings_path(name))throw resource::Error("Invalid original settings filename");
        try{return open_resource_file(directory/native_utf8_path(name));}catch(const resource::NotFound&){return {};}
    };
}
game::LegacySaveSources native_desktop_legacy_saves(const std::filesystem::path& directory){
    if(directory.empty() || !directory.is_absolute())throw std::invalid_argument("Native original-save directory must be absolute");const auto root=directory.lexically_normal();game::LegacySaveSources result;
    result.open=[root](std::string_view name)->std::shared_ptr<resource::Source>{if(!resource::legacy_save_path(name))throw resource::Error("Invalid original manual-save filename");try{return open_resource_file(root/native_utf8_path(name));}catch(const resource::NotFound&){return {};}};
    result.list=[root]{std::vector<game::LegacySaveFile> files;
        for(const auto prefix:resource::legacy_save_directories){const auto directory=prefix.empty()?root:root/native_utf8_path(prefix);std::error_code error;const bool exists=std::filesystem::exists(directory,error);if(error)throw std::filesystem::filesystem_error("Cannot inspect original save directory",directory,error);if(!exists)continue;if(!std::filesystem::is_directory(directory))throw resource::Error("Original save directory has the wrong file type");unsigned count{};
            for(const auto& item:std::filesystem::directory_iterator(directory)){if(++count>10000)throw resource::Error("Original save directory has too many entries");const auto utf8=item.path().filename().u8string();std::string name(utf8.begin(),utf8.end());name=std::string(prefix)+name;if(!resource::legacy_save_path(name))continue;game::LegacySaveFile file;file.name=std::move(name);
                try{if(!item.is_regular_file())throw resource::Error("Original manual save is not an ordinary file");file.bytes=item.file_size();const auto stamp=item.last_write_time();const auto modified=std::chrono::duration_cast<std::chrono::milliseconds>(stamp-decltype(stamp)::clock::now()+std::chrono::system_clock::now().time_since_epoch()).count();file.modified_ms=modified<0?0:std::uint64_t(modified);}catch(const std::exception& error){file.diagnostic=error.what();}files.push_back(std::move(file));}
        }return files;
    };return result;
}
}
