#include "progress_storage.hpp"
#include "legacy_save_path.hpp"
#include <algorithm>

namespace wa2::platform {
namespace {
class ProgressFile final:public progress::Storage {
    std::shared_ptr<GameDirectory> directory_;
    bool combined_;
public:
    ProgressFile(std::shared_ptr<GameDirectory> directory,bool combined):directory_(std::move(directory)),combined_(combined){if(!directory_)throw std::invalid_argument("Missing selected progress directory");}
    std::optional<progress::Bytes> load()override{
        constexpr auto size=progress::layout::system_save_bytes;
        if(auto bytes=game_file_read(*directory_,"Sys.sav",size,size))return bytes;
        // Select one installation; a root file, including corrupt files, wins.
        const auto name=std::string(resource::legacy_save_directories[combined_?1:2])+"Sys.sav";
        if(auto bytes=game_file_read(*directory_,name,size,size)){progress::State::decode(*bytes);directory_->replace("Sys.sav",*bytes);return bytes;}return {};
    }
    void write(progress::Block block,std::span<const std::uint8_t> bytes)override{
        const auto at=progress::offset(block);if(bytes.size()!=progress::size(block))throw progress::Error("Invalid progress write block length");
        constexpr auto size=progress::layout::system_save_bytes;auto source=directory_->open("Sys.sav");
        if(!source){progress::Bytes initial(size);std::copy(bytes.begin(),bytes.end(),initial.begin()+at);directory_->replace("Sys.sav",initial);return;}
        if(source->size()!=size)throw progress::Error("Unsupported Sys.sav length");source.reset();directory_->update("Sys.sav",size,at,bytes);
    }
};
}
std::shared_ptr<progress::Storage> progress_storage(std::shared_ptr<GameDirectory> directory,bool combined){return std::make_shared<ProgressFile>(std::move(directory),combined);}
std::shared_ptr<progress::Storage> progress_storage(const std::filesystem::path& directory){return progress_storage(game_directory(directory));}
}
