#include "test_profile.hpp"
#include "../../engine/test_progress.hpp"
#include <fstream>
#include <stdexcept>
#include <vector>

namespace wa2::platform::compat {
namespace {
void directory(const std::filesystem::path& path){
    std::error_code error;const auto status=std::filesystem::symlink_status(path,error);
    if(error && error!=std::errc::no_such_file_or_directory)throw std::filesystem::filesystem_error("Cannot inspect test directory",path,error);
    if(std::filesystem::exists(status)){
        if(!std::filesystem::is_directory(status) || std::filesystem::is_symlink(status))throw std::runtime_error("Test profile directory must be an ordinary directory");
    }else if(!std::filesystem::create_directory(path))throw std::runtime_error("Cannot create test profile directory");
}
void seed(const std::filesystem::path& file){
    std::error_code error;const auto status=std::filesystem::symlink_status(file,error);
    if(error && error!=std::errc::no_such_file_or_directory)throw std::filesystem::filesystem_error("Cannot inspect test system save",file,error);
    if(std::filesystem::exists(status)){
        if(!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status) || std::filesystem::hard_link_count(file)!=1)
            throw std::runtime_error("Test system save must be a private ordinary file");
        if(std::filesystem::file_size(file)!=testing::system_save_bytes)throw std::runtime_error("Unsupported test Sys.sav length; normal saves were not changed");
    }else{
        std::vector<std::uint8_t> data(testing::system_save_bytes);
        testing::unlock_all_content(data);
        std::ofstream output(file,std::ios::binary|std::ios::trunc);
        output.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));output.close();
        if(!output)throw std::runtime_error("Cannot create test Sys.sav");
        return;
    }
    std::fstream stream(file,std::ios::binary|std::ios::in|std::ios::out);
    std::vector<std::uint8_t> data(testing::system_save_bytes);
    stream.read(reinterpret_cast<char*>(data.data()),std::streamsize(data.size()));
    if(!stream)throw std::runtime_error("Cannot read test Sys.sav");
    if(testing::unlock_all_content(data)){
        stream.seekp(0);stream.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));stream.flush();
        if(!stream)throw std::runtime_error("Cannot unlock test Sys.sav content");
    }
}
}
std::filesystem::path select_test_profile(const std::filesystem::path& normal_root,bool enabled){
    if(!enabled)return normal_root;
    if(!std::filesystem::is_directory(normal_root))throw std::runtime_error("Missing normal data directory for test profile");
    const auto root=normal_root/"test-mode";directory(root);directory(root/"Documents");directory(root/"Documents"/"Leaf");
    for(const auto* name:{"WHITE ALBUM2","WHITE ALBUM2 -closing chapter-","WHITE ALBUM2 -introductory chapter-"}){
        const auto folder=root/"Documents"/"Leaf"/name;directory(folder);seed(folder/"Sys.sav");
    }
    return root;
}
}
