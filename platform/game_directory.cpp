#include "game_directory.hpp"
#include "legacy_save_path.hpp"
#include "private_file.hpp"
#include "file_io.hpp"
#include <algorithm>
#include <fstream>
#include <limits>
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
std::optional<unsigned> game_slot_file(std::string_view name)noexcept{
    if(name.size()!=15 || !name.starts_with("slot-") || !name.ends_with(".wa2sav"))return {};unsigned slot{};
    for(unsigned n=5;n<8;++n){if(name[n]<'0' || name[n]>'9')return {};slot=slot*10+unsigned(name[n]-'0');}return slot;
}
bool game_file_path(std::string_view name,bool writable)noexcept{
    if(name.starts_with("native-test/")){const auto child=name.substr(12);return !child.starts_with("native-test/") && game_file_path(child,true);}
    if(name=="Sys.sav" || name=="native-settings.wa2cfg" || name=="native-launcher.wa2cfg")return true;
    if(name.starts_with("native-saves/") && game_slot_file(name.substr(13)))return true;
    if(!writable)for(const auto prefix:resource::legacy_save_directories)if(name.size()==prefix.size()+7 && name.starts_with(prefix) && name.substr(prefix.size())=="Sys.sav")return true;
    return false;
}
std::optional<std::vector<std::uint8_t>> game_file_read(GameDirectory& directory,std::string_view name,std::uint64_t minimum,std::uint64_t maximum,std::optional<std::size_t> prefix){
    if(!game_file_path(name) || minimum>maximum || (prefix && !*prefix))throw std::invalid_argument("Invalid game-file read bounds or path");
    auto source=directory.open(name);if(!source)return {};const auto size=source->size();if(size<minimum || size>maximum)throw resource::Error("Unsupported game-file length: "+std::string(name));
    const auto count=prefix?std::min<std::uint64_t>(size,*prefix):size;if(count>std::numeric_limits<std::size_t>::max())throw resource::Error("Game file exceeds host read bounds");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(count));if(!bytes.empty())source->read_at(0,bytes);if(source->size()!=size)throw resource::Error("Game file changed during read");return bytes;
}
namespace {
namespace fs=std::filesystem;
class StoredSource final:public resource::Source {
    std::shared_ptr<resource::Source> source_;
    fs::path file_;
    std::uint64_t bytes_;
public:
    StoredSource(std::shared_ptr<resource::Source> source,fs::path file,std::uint64_t bytes):source_(std::move(source)),file_(std::move(file)),bytes_(bytes){if(source_->size()!=bytes_)throw resource::Error("Game file changed while opening");}
    std::uint64_t size()const override{private_file::require_directory(file_.parent_path());if(private_file::size(file_,bytes_,bytes_)!=bytes_)throw resource::Error("Game file disappeared during read");return bytes_;}
    void read_at(std::uint64_t offset,std::span<std::uint8_t> bytes)override{source_->read_at(offset,bytes);}
};
class Directory final:public GameDirectory {
    fs::path root_;
    std::optional<fs::path> path(std::string_view name,bool create=false){
        if(!game_file_path(name,create))throw resource::Error("Invalid selected-directory storage path");private_file::require_directory(root_);auto parent=root_;const auto relative=fs::path(std::string(name));
        for(const auto& component:relative.parent_path()){parent/=component;std::error_code error;const auto status=fs::symlink_status(parent,error);if(error && error!=std::errc::no_such_file_or_directory)throw fs::filesystem_error("Cannot inspect game save directory",parent,error);
            if(!fs::exists(status)){if(!create)return {};fs::create_directory(parent);}private_file::require_directory(parent);}
        return root_/relative;
    }
    static std::uint64_t modified(const fs::path& path){
#if defined(_WIN32)
        WIN32_FILE_ATTRIBUTE_DATA value;if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&value))throw std::system_error(int(GetLastError()),std::system_category(),"Cannot inspect game save time");const auto ticks=(std::uint64_t(value.ftLastWriteTime.dwHighDateTime)<<32)|value.ftLastWriteTime.dwLowDateTime;constexpr std::uint64_t epoch=116444736000000000ull;return ticks<epoch?0:(ticks-epoch)/10000;
#else
        struct stat value{};if(lstat(path.c_str(),&value))throw std::system_error(errno,std::generic_category(),"Cannot inspect game save time");return value.st_mtime<0?0:std::uint64_t(value.st_mtime)*1000;
#endif
    }
public:
    explicit Directory(fs::path root):root_(fs::absolute(std::move(root)).lexically_normal()){}
    std::shared_ptr<resource::Source> open(std::string_view name)override{const auto file=path(name);if(!file)return {};const auto bytes=private_file::size(*file);if(!bytes)return {};return std::make_shared<StoredSource>(open_resource_file(*file),*file,*bytes);}
    void replace(std::string_view name,std::span<const std::uint8_t> bytes)override{if(!game_file_path(name,true))throw resource::Error("Invalid game-file write path");private_file::replace(*path(name,true),bytes,0,64u*1024*1024);}
    void update(std::string_view name,std::uint64_t expected,std::uint64_t offset,std::span<const std::uint8_t> bytes)override{
        if(!game_file_path(name,true) || expected>64u*1024*1024 || offset>expected || bytes.size()>expected-offset)throw resource::Error("Invalid game-file update range");const auto file=path(name);if(!file || private_file::size(*file,expected,expected)!=expected)throw resource::Error("Game-file update needs an existing complete file");
        std::fstream output(*file,std::ios::binary|std::ios::in|std::ios::out);output.seekp(std::streamoff(offset));output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));output.close();if(!output)throw resource::Error("Cannot persist game-file range");
    }
    std::vector<GameFileInfo> saves(std::string_view directory)override{
        if(directory!="native-saves" && directory!="native-test/native-saves")throw resource::Error("Invalid native save inventory directory");
        std::vector<GameFileInfo> result;const auto probe=path(std::string(directory)+"/slot-000.wa2sav");if(!probe)return result;unsigned count{};
        for(const auto& item:fs::directory_iterator(probe->parent_path())){if(++count>10000)throw resource::Error("Native save directory has too many entries");const auto utf8=item.path().filename().u8string();std::string name(utf8.begin(),utf8.end());if(!game_slot_file(name))continue;GameFileInfo info;info.name=std::move(name);
            try{const auto size=private_file::size(item.path(),16,64u*1024*1024);if(!size)throw resource::Error("Game save disappeared during inventory");info.bytes=*size;info.modified_ms=modified(item.path());}catch(const std::exception& error){info.diagnostic=error.what();}result.push_back(std::move(info));}return result;
    }
};
class TestDirectory final:public GameDirectory {
    std::shared_ptr<GameDirectory> parent_;
    static std::string path(std::string_view name){if(name.starts_with("native-test/") || !game_file_path(name,true))throw resource::Error("Invalid test-profile storage path");return "native-test/"+std::string(name);}
public:
    explicit TestDirectory(std::shared_ptr<GameDirectory> parent):parent_(std::move(parent)){if(!parent_)throw std::invalid_argument("Missing test-profile game directory");}
    std::shared_ptr<resource::Source> open(std::string_view name)override{if(!game_file_path(name))throw resource::Error("Invalid test-profile read path");if(!game_file_path(name,true))return {};return parent_->open(path(name));}
    void replace(std::string_view name,std::span<const std::uint8_t> bytes)override{parent_->replace(path(name),bytes);}
    void update(std::string_view name,std::uint64_t expected,std::uint64_t offset,std::span<const std::uint8_t> bytes)override{parent_->update(path(name),expected,offset,bytes);}
    std::vector<GameFileInfo> saves(std::string_view directory)override{if(directory!="native-saves")throw resource::Error("Invalid nested test-profile save directory");return parent_->saves("native-test/native-saves");}
};
}
std::shared_ptr<GameDirectory> game_directory(const std::filesystem::path& root){return std::make_shared<Directory>(root);}
std::shared_ptr<GameDirectory> game_test_directory(std::shared_ptr<GameDirectory> parent){return std::make_shared<TestDirectory>(std::move(parent));}
}
