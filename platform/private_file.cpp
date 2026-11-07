#include "private_file.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <system_error>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace wa2::platform::private_file {
namespace fs=std::filesystem;
void require_directory(const fs::path& path){const auto status=fs::symlink_status(path);if(!fs::is_directory(status) || fs::is_symlink(status))throw std::runtime_error("Storage directory must be an ordinary private directory");}
std::optional<std::uint64_t> size(const fs::path& path,std::uint64_t minimum,std::uint64_t maximum){
    if(minimum>maximum)throw std::invalid_argument("Invalid private-file size bounds");std::error_code error;const auto status=fs::symlink_status(path,error);
    if(error && error!=std::errc::no_such_file_or_directory)throw fs::filesystem_error("Cannot inspect private file",path,error);
    if(!fs::exists(status))return {};
    if(!fs::is_regular_file(status) || fs::is_symlink(status) || fs::hard_link_count(path)!=1)throw std::runtime_error("Storage file must be a private ordinary file");
    const auto bytes=fs::file_size(path);if(bytes<minimum || bytes>maximum)throw std::runtime_error("Private file size is outside supported bounds");return bytes;
}
std::optional<std::vector<std::uint8_t>> read(const fs::path& path,std::uint64_t minimum,std::uint64_t maximum){
    require_directory(path.parent_path());const auto count=size(path,minimum,maximum);if(!count)return {};
    if(*count>std::uint64_t(std::numeric_limits<std::streamsize>::max()) || *count>std::numeric_limits<std::size_t>::max())throw std::runtime_error("Private file exceeds host read bounds");
    std::ifstream input(path,std::ios::binary);std::vector<std::uint8_t> bytes(static_cast<std::size_t>(*count));input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));
    if(!input || input.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Cannot read a stable private file");return bytes;
}
void replace(const fs::path& target,std::span<const std::uint8_t> bytes,std::uint64_t minimum,std::uint64_t maximum){
    if(minimum>maximum || bytes.size()<minimum || bytes.size()>maximum)throw std::invalid_argument("Private-file write is outside supported bounds");
    const auto directory=target.parent_path();require_directory(directory);size(target);static std::atomic<std::uint64_t> sequence{};fs::path temporary;bool owned{};
    try{
        for(unsigned attempt=0;attempt<16;++attempt){temporary=target;temporary+=fs::path(".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence.fetch_add(1)));
#if defined(_WIN32)
            const auto handle=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(handle==INVALID_HANDLE_VALUE){const auto error=GetLastError();if(error==ERROR_FILE_EXISTS || error==ERROR_ALREADY_EXISTS)continue;throw std::system_error(int(error),std::system_category(),"Cannot create private-file temporary");}
            owned=true;bool closed=false;try{std::size_t at{};while(at<bytes.size()){DWORD count{};const auto request=DWORD(std::min<std::size_t>(bytes.size()-at,std::numeric_limits<DWORD>::max()));if(!WriteFile(handle,bytes.data()+at,request,&count,nullptr) || !count)throw std::system_error(int(GetLastError()),std::system_category(),"Cannot write private-file temporary");at+=count;}
                if(!FlushFileBuffers(handle))throw std::system_error(int(GetLastError()),std::system_category(),"Cannot flush private-file temporary");const auto result=CloseHandle(handle);closed=true;if(!result)throw std::system_error(int(GetLastError()),std::system_category(),"Cannot close private-file temporary");}catch(...){if(!closed)CloseHandle(handle);throw;}
#else
            const auto descriptor=open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
            if(descriptor<0){if(errno==EEXIST)continue;throw std::system_error(errno,std::generic_category(),"Cannot create private-file temporary");}
            owned=true;bool closed=false;try{std::size_t at{};while(at<bytes.size()){const auto count=::write(descriptor,bytes.data()+at,bytes.size()-at);if(count<0 && errno==EINTR)continue;if(count<=0)throw std::system_error(errno,std::generic_category(),"Cannot write private-file temporary");at+=std::size_t(count);}
                if(fsync(descriptor))throw std::system_error(errno,std::generic_category(),"Cannot flush private-file temporary");const auto result=close(descriptor);closed=true;if(result)throw std::system_error(errno,std::generic_category(),"Cannot close private-file temporary");}catch(...){if(!closed)close(descriptor);throw;}
#endif
            break;
        }
        if(!owned)throw std::runtime_error("Cannot allocate a unique private-file temporary");require_directory(directory);size(target);
#if defined(_WIN32)
        if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::system_error(int(GetLastError()),std::system_category(),"Cannot publish private file");
#else
        const auto directory_fd=open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(directory_fd<0)throw std::system_error(errno,std::generic_category(),"Cannot open private directory for publication");
        if(renameat(directory_fd,temporary.filename().c_str(),directory_fd,target.filename().c_str())){const auto error=errno;close(directory_fd);throw std::system_error(error,std::generic_category(),"Cannot publish private file");}
        owned=false;const auto synced=fsync(directory_fd),error=errno;close(directory_fd);if(synced)throw std::system_error(error,std::generic_category(),"Cannot flush private storage directory");
#endif
        owned=false;
    }catch(...){if(owned){std::error_code ignored;fs::remove(temporary,ignored);}throw;}
}
}
