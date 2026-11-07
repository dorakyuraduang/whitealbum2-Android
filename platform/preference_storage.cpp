#include "preference_storage.hpp"
#include "private_file.hpp"
#include "preferences_ini.hpp"

namespace wa2::platform {
namespace {
class PreferenceFile final:public game::PreferenceStorage {
    std::filesystem::path file_;
    LegacySettingsOpener legacy_;
public:
    explicit PreferenceFile(std::filesystem::path directory,LegacySettingsOpener legacy):file_(std::filesystem::absolute(std::move(directory)).lexically_normal()/"native-settings.wa2cfg"),legacy_(std::move(legacy)){}
    std::optional<std::vector<std::uint8_t>> read()override{return private_file::read(file_,16,4096);}
    std::optional<game::Preferences> legacy()override{
        if(!legacy_)return {};
        const auto read=[&](std::string_view name)->std::optional<std::vector<std::uint8_t>>{
            auto source=legacy_(name);if(!source)return {};const auto length=source->size();if(length>65536)throw resource::Error("Original settings file exceeds 64 KiB: "+std::string(name));
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));if(!bytes.empty())source->read_at(0,bytes);return bytes;
        };
        for(const auto prefix:{"","Documents/Leaf/WHITE ALBUM2/"}){
            const auto system=read(std::string(prefix)+"SYSTEM.ini"),display=read(std::string(prefix)+"D3D.ini");
            // Never merge settings from two installations/directories.
            if(system || display)return game::import_preferences_ini(system?std::span<const std::uint8_t>(*system):std::span<const std::uint8_t>{},display?std::span<const std::uint8_t>(*display):std::span<const std::uint8_t>{});
        }
        return {};
    }
    void replace(std::span<const std::uint8_t> bytes)override{private_file::replace(file_,bytes,16,4096);}
};
}
std::shared_ptr<game::PreferenceStorage> preference_storage(const std::filesystem::path& directory,LegacySettingsOpener legacy){return std::make_shared<PreferenceFile>(directory,std::move(legacy));}
}
