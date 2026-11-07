#include "save_storage.hpp"
#include "checkpoint_codec.hpp"
#include "private_file.hpp"
#include <array>
#include <cerrno>
#include <cstdio>
#include <system_error>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace wa2::platform {
namespace {
namespace fs=std::filesystem;
void ordinary_directory(const fs::path& path){private_file::require_directory(path);}
std::optional<std::uint64_t> ordinary_file(const fs::path& path,bool check_size=true){return private_file::size(path,check_size?16:0,check_size?checkpoint::maximum_bytes:std::numeric_limits<std::uint64_t>::max());}
std::string filename(unsigned slot){if(slot>=game::native_save_slots)throw std::out_of_range("Native save slot is out of range");std::array<char,32> name{};std::snprintf(name.data(),name.size(),"slot-%03u.wa2sav",slot);return name.data();}
std::string owned_name(const fs::path& path){std::string name;for(auto c:path.filename().native()){if(std::uint32_t(c)>127)return {};name.push_back(char(c));}return name;}
std::uint64_t modified(const fs::path& path){
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA value;if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&value))throw std::system_error(int(GetLastError()),std::system_category(),"Cannot inspect native save time");const auto ticks=(std::uint64_t(value.ftLastWriteTime.dwHighDateTime)<<32)|value.ftLastWriteTime.dwLowDateTime;constexpr std::uint64_t epoch=116444736000000000ull;return ticks<epoch?0:(ticks-epoch)/10000;
#else
    struct stat value{};if(lstat(path.c_str(),&value))throw std::system_error(errno,std::generic_category(),"Cannot inspect native save time");return value.st_mtime<0?0:std::uint64_t(value.st_mtime)*1000;
#endif
}
class SaveFiles final : public game::SaveStorage {
    fs::path root_,directory_;
    bool directory(bool create){ordinary_directory(root_);std::error_code error;const auto status=fs::symlink_status(directory_,error);if(error && error!=std::errc::no_such_file_or_directory)throw fs::filesystem_error("Cannot inspect native save directory",directory_,error);if(!fs::exists(status)){if(!create)return false;fs::create_directory(directory_);}ordinary_directory(directory_);return true;}
public:
    explicit SaveFiles(fs::path root):root_(fs::absolute(std::move(root)).lexically_normal()),directory_(root_/"native-saves"){}
    std::optional<game::SavedScene::Bytes> read(unsigned slot)override{const auto name=filename(slot);if(!directory(false))return {};return private_file::read(directory_/name,16,checkpoint::maximum_bytes);}
    void replace(unsigned slot,std::span<const std::uint8_t> bytes)override{const auto name=filename(slot);if(bytes.size()<16 || bytes.size()>checkpoint::maximum_bytes)throw std::runtime_error("Native save write exceeds size bounds");directory(true);private_file::replace(directory_/name,bytes,16,checkpoint::maximum_bytes);}
    std::vector<game::SaveSlotInfo> list()override{std::vector<game::SaveSlotInfo> result;if(!directory(false))return result;for(const auto& entry:fs::directory_iterator(directory_)){const auto name=owned_name(entry.path());if(name.size()!=15 || name.substr(0,5)!="slot-" || name.substr(8)!=".wa2sav")continue;unsigned slot{};bool numeric=true;for(unsigned n=5;n<8;++n){if(name[n]<'0' || name[n]>'9'){numeric=false;break;}slot=slot*10+unsigned(name[n]-'0');}if(!numeric)continue;game::SaveSlotInfo info;info.slot=slot;try{const auto bytes=ordinary_file(entry.path());if(!bytes)throw std::runtime_error("Native save disappeared during inventory");info.bytes=*bytes;info.modified_ms=modified(entry.path());}catch(const std::exception& error){info.diagnostic=error.what();}result.push_back(std::move(info));}return result;}
};
}
std::shared_ptr<game::SaveStorage> save_storage(const std::filesystem::path& directory){return std::make_shared<SaveFiles>(directory);}
}
