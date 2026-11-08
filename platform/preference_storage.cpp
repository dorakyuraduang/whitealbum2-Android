#include "preference_storage.hpp"
#include "preferences_ini.hpp"

namespace wa2::platform {
namespace {
class PreferenceFile final:public game::PreferenceStorage {
    std::shared_ptr<GameDirectory> directory_;
    LegacySettingsOpener legacy_;
public:
    explicit PreferenceFile(std::shared_ptr<GameDirectory> directory,LegacySettingsOpener legacy):directory_(std::move(directory)),legacy_(std::move(legacy)){if(!directory_)throw std::invalid_argument("Missing selected preference directory");}
    std::optional<std::vector<std::uint8_t>> read()override{return game_file_read(*directory_,"native-settings.wa2cfg",16,4096);}
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
    void replace(std::span<const std::uint8_t> bytes)override{if(bytes.size()<16 || bytes.size()>4096)throw std::invalid_argument("Native settings exceed size bounds");directory_->replace("native-settings.wa2cfg",bytes);}
};
}
std::shared_ptr<game::PreferenceStorage> preference_storage(std::shared_ptr<GameDirectory> directory,LegacySettingsOpener legacy){return std::make_shared<PreferenceFile>(std::move(directory),std::move(legacy));}
std::shared_ptr<game::PreferenceStorage> preference_storage(const std::filesystem::path& directory,LegacySettingsOpener legacy){return preference_storage(game_directory(directory),std::move(legacy));}
}
