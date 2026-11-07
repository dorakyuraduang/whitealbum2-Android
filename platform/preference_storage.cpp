#include "preference_storage.hpp"
#include "private_file.hpp"

namespace wa2::platform {
namespace {
class PreferenceFile final:public game::PreferenceStorage {
    std::filesystem::path file_;
public:
    explicit PreferenceFile(std::filesystem::path directory):file_(std::filesystem::absolute(std::move(directory)).lexically_normal()/"native-settings.wa2cfg"){}
    std::optional<std::vector<std::uint8_t>> read()override{return private_file::read(file_,16,4096);}
    void replace(std::span<const std::uint8_t> bytes)override{private_file::replace(file_,bytes,16,4096);}
};
}
std::shared_ptr<game::PreferenceStorage> preference_storage(const std::filesystem::path& directory){return std::make_shared<PreferenceFile>(directory);}
}
