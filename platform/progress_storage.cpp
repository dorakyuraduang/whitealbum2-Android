#include "progress_storage.hpp"
#include <algorithm>
#include <array>
#include <fstream>

namespace wa2::platform {
namespace {
class ProgressFile final : public progress::Storage {
    std::filesystem::path directory_,file_;
    bool exists(){
        const auto directory=std::filesystem::symlink_status(directory_);
        if(!std::filesystem::is_directory(directory) || std::filesystem::is_symlink(directory))throw progress::Error("Progress directory must be an ordinary private directory");
        std::error_code error;const auto status=std::filesystem::symlink_status(file_,error);
        if(error && error!=std::errc::no_such_file_or_directory)throw std::filesystem::filesystem_error("Cannot inspect progress save",file_,error);
        if(!std::filesystem::exists(status))return false;
        if(!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status) || std::filesystem::hard_link_count(file_)!=1)
            throw progress::Error("Progress save must be a private ordinary file");
        if(std::filesystem::file_size(file_)!=progress::layout::system_save_bytes)throw progress::Error("Unsupported Sys.sav length");
        return true;
    }
public:
    explicit ProgressFile(std::filesystem::path directory):directory_(std::move(directory)),file_(directory_/"Sys.sav"){}
    std::optional<progress::Bytes> load()override{
        if(!exists())return {};
        std::ifstream input(file_,std::ios::binary);progress::Bytes bytes(progress::layout::system_save_bytes);
        input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));
        if(!input)throw progress::Error("Cannot read native progress save");return bytes;
    }
    void write(progress::Block block,std::span<const std::uint8_t> bytes)override{
        const auto at=progress::offset(block);
        if(bytes.size()!=progress::size(block))throw progress::Error("Invalid progress write block length");
        if(!exists()){
            std::ofstream output(file_,std::ios::binary|std::ios::trunc);
            std::array<char,65536> zeros{};std::size_t remaining=progress::layout::system_save_bytes;
            while(output && remaining){const auto count=std::min(remaining,zeros.size());output.write(zeros.data(),std::streamsize(count));remaining-=count;}
            output.close();if(!output)throw progress::Error("Cannot initialize native progress save");
        }
        std::fstream output(file_,std::ios::binary|std::ios::in|std::ios::out);
        output.seekp(std::streamoff(at));output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
        output.close();if(!output)throw progress::Error("Cannot persist native progress block");
    }
};
}
std::shared_ptr<progress::Storage> progress_storage(const std::filesystem::path& private_directory){return std::make_shared<ProgressFile>(private_directory);}
}
