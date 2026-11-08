#include "save_storage.hpp"
#include "checkpoint_codec.hpp"
#include <array>
#include <cstdio>
#include <set>

namespace wa2::platform {
namespace {
std::string filename(unsigned slot){if(slot>=game::native_save_slots)throw std::out_of_range("Native save slot is out of range");std::array<char,48> name{};std::snprintf(name.data(),name.size(),"native-saves/slot-%03u.wa2sav",slot);return name.data();}
class SaveFiles final:public game::SaveStorage {
    std::shared_ptr<GameDirectory> directory_;
public:
    explicit SaveFiles(std::shared_ptr<GameDirectory> directory):directory_(std::move(directory)){if(!directory_)throw std::invalid_argument("Missing selected save directory");}
    std::optional<game::SavedScene::Bytes> read(unsigned slot)override{return game_file_read(*directory_,filename(slot),16,checkpoint::maximum_bytes);}
    std::optional<game::SavedScene::Bytes> prefix(unsigned slot,std::size_t count)override{if(!count || count>game::SaveDocument::prefix_bytes)throw std::invalid_argument("Native save preview request exceeds its limit");return game_file_read(*directory_,filename(slot),16,checkpoint::maximum_bytes,count);}
    void replace(unsigned slot,std::span<const std::uint8_t> bytes)override{const auto name=filename(slot);if(bytes.size()<16 || bytes.size()>checkpoint::maximum_bytes)throw std::runtime_error("Native save write exceeds size bounds");directory_->replace(name,bytes);}
    std::vector<game::SaveSlotInfo> list()override{
        auto files=directory_->saves();if(files.size()>game::native_save_slots)throw resource::Error("Native save inventory exceeds its bound");std::vector<game::SaveSlotInfo> result;std::set<unsigned> seen;
        for(auto& file:files){const auto slot=game_slot_file(file.name);if(!slot || !seen.insert(*slot).second)throw resource::Error("Unknown or duplicate native save filename");if(file.diagnostic.empty() && (file.bytes<16 || file.bytes>checkpoint::maximum_bytes))file.diagnostic="Native save length is outside supported bounds";result.push_back({*slot,file.bytes,file.modified_ms,std::move(file.diagnostic)});}return result;
    }
};
}
std::shared_ptr<game::SaveStorage> save_storage(std::shared_ptr<GameDirectory> directory){return std::make_shared<SaveFiles>(std::move(directory));}
std::shared_ptr<game::SaveStorage> save_storage(const std::filesystem::path& directory){return save_storage(game_directory(directory));}
}
