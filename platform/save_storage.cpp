#include "save_storage.hpp"
#include "checkpoint_codec.hpp"
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <system_error>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace wa2::platform {
namespace {
namespace fs=std::filesystem;
void ordinary_directory(const fs::path& path){const auto status=fs::symlink_status(path);if(!fs::is_directory(status) || fs::is_symlink(status))throw std::runtime_error("Native save directory must be an ordinary private directory");}
std::optional<std::uint64_t> ordinary_file(const fs::path& path,bool check_size=true){std::error_code error;const auto status=fs::symlink_status(path,error);if(error && error!=std::errc::no_such_file_or_directory)throw fs::filesystem_error("Cannot inspect native save",path,error);if(!fs::exists(status))return {};if(!fs::is_regular_file(status) || fs::is_symlink(status) || fs::hard_link_count(path)!=1)throw std::runtime_error("Native save must be a private ordinary file");const auto bytes=fs::file_size(path);if(check_size && (bytes<16 || bytes>checkpoint::maximum_bytes))throw std::runtime_error("Native save size is outside supported bounds");return bytes;}
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
    std::optional<game::SavedScene::Bytes> read(unsigned slot)override{const auto name=filename(slot);if(!directory(false))return {};const auto path=directory_/name;const auto bytes=ordinary_file(path);if(!bytes)return {};std::ifstream input(path,std::ios::binary);game::SavedScene::Bytes result(static_cast<std::size_t>(*bytes));input.read(reinterpret_cast<char*>(result.data()),std::streamsize(result.size()));if(!input || input.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Cannot read a stable native save file");return result;}
    void replace(unsigned slot,std::span<const std::uint8_t> bytes)override{
        const auto name=filename(slot);if(bytes.size()<16 || bytes.size()>checkpoint::maximum_bytes)throw std::runtime_error("Native save write exceeds size bounds");directory(true);const auto target=directory_/name;ordinary_file(target,false);static std::atomic<std::uint64_t> sequence{};fs::path temporary;bool owned=false;
        try{
            for(unsigned attempt=0;attempt<16;++attempt){temporary=directory_/(name+".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence.fetch_add(1)));
#if defined(_WIN32)
                const auto handle=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(handle==INVALID_HANDLE_VALUE){const auto error=GetLastError();if(error==ERROR_FILE_EXISTS || error==ERROR_ALREADY_EXISTS)continue;throw std::system_error(int(error),std::system_category(),"Cannot create native save temporary");}owned=true;bool closed=false;try{std::size_t at{};while(at<bytes.size()){DWORD count{};if(!WriteFile(handle,bytes.data()+at,DWORD(bytes.size()-at),&count,nullptr) || !count)throw std::system_error(int(GetLastError()),std::system_category(),"Cannot write native save temporary");at+=count;}if(!FlushFileBuffers(handle))throw std::system_error(int(GetLastError()),std::system_category(),"Cannot flush native save temporary");if(!CloseHandle(handle))throw std::system_error(int(GetLastError()),std::system_category(),"Cannot close native save temporary");closed=true;}catch(...){if(!closed)CloseHandle(handle);throw;}
#else
                const auto descriptor=open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(descriptor<0){if(errno==EEXIST)continue;throw std::system_error(errno,std::generic_category(),"Cannot create native save temporary");}owned=true;bool closed=false;try{std::size_t at{};while(at<bytes.size()){const auto count=::write(descriptor,bytes.data()+at,bytes.size()-at);if(count<0 && errno==EINTR)continue;if(count<=0)throw std::system_error(errno,std::generic_category(),"Cannot write native save temporary");at+=std::size_t(count);}if(fsync(descriptor))throw std::system_error(errno,std::generic_category(),"Cannot flush native save temporary");if(close(descriptor))throw std::system_error(errno,std::generic_category(),"Cannot close native save temporary");closed=true;}catch(...){if(!closed)close(descriptor);throw;}
#endif
                break;
            }
            if(!owned)throw std::runtime_error("Cannot allocate a unique native save temporary");ordinary_directory(root_);ordinary_directory(directory_);ordinary_file(target,false);
#if defined(_WIN32)
            if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::system_error(int(GetLastError()),std::system_category(),"Cannot publish native save");
#else
            const auto directory_fd=open(directory_.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(directory_fd<0)throw std::system_error(errno,std::generic_category(),"Cannot open native save directory for publication");
            if(renameat(directory_fd,temporary.filename().c_str(),directory_fd,target.filename().c_str())){const auto error=errno;close(directory_fd);throw std::system_error(error,std::generic_category(),"Cannot publish native save");}owned=false;const auto synced=fsync(directory_fd),error=errno;close(directory_fd);if(synced)throw std::system_error(error,std::generic_category(),"Cannot flush native save directory");
#endif
            owned=false;
        }catch(...){if(owned){std::error_code ignored;fs::remove(temporary,ignored);}throw;}
    }
    std::vector<game::SaveSlotInfo> list()override{std::vector<game::SaveSlotInfo> result;if(!directory(false))return result;for(const auto& entry:fs::directory_iterator(directory_)){const auto name=owned_name(entry.path());if(name.size()!=15 || name.substr(0,5)!="slot-" || name.substr(8)!=".wa2sav")continue;unsigned slot{};bool numeric=true;for(unsigned n=5;n<8;++n){if(name[n]<'0' || name[n]>'9'){numeric=false;break;}slot=slot*10+unsigned(name[n]-'0');}if(!numeric)continue;game::SaveSlotInfo info;info.slot=slot;try{const auto bytes=ordinary_file(entry.path());if(!bytes)throw std::runtime_error("Native save disappeared during inventory");info.bytes=*bytes;info.modified_ms=modified(entry.path());}catch(const std::exception& error){info.diagnostic=error.what();}result.push_back(std::move(info));}return result;}
};
}
std::shared_ptr<game::SaveStorage> save_storage(const std::filesystem::path& directory){return std::make_shared<SaveFiles>(directory);}
}
