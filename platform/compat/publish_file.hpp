#pragma once
#include <chrono>
#include <filesystem>
#include <thread>
namespace wa2::platform::compat {
// A closed staged file can briefly be locked by another Windows process.
// Never delete the old destination or retry unrelated errors. Publication is
// still synchronous; a persistent denial fails after at most 30 ms of waits.
template<class Rename,class Pause>
void publish_closed_file(const std::filesystem::path& staged,const std::filesystem::path& target,Rename rename,Pause pause){
    for(unsigned attempt=0;;++attempt){
        std::error_code error;rename(staged,target,error);if(!error)return;
#ifdef _WIN32
        if(error==std::errc::permission_denied && attempt<4){pause(std::chrono::milliseconds(2u<<attempt));continue;}
#endif
        throw std::filesystem::filesystem_error("Cannot publish closed file",staged,target,error);
    }
}
inline void publish_closed_file(const std::filesystem::path& staged,const std::filesystem::path& target){
    publish_closed_file(staged,target,[](const auto& from,const auto& to,std::error_code& error){std::filesystem::rename(from,to,error);},
        [](auto delay){std::this_thread::sleep_for(delay);});
}
}
