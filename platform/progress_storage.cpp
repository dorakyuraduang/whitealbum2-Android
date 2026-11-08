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
        auto bytes=game_file_read(*directory_,"Sys.sav",size,size);const bool root=bytes.has_value();
        // Select one primary installation. Corrupt root files fail before any
        // fallback or merge. The separate introductory bank is the one source-
        // proven merge, not a general union of every copied save directory.
        if(!root)bytes=game_file_read(*directory_,std::string(resource::legacy_save_directories[combined_?1:2])+"Sys.sav",size,size);
        const auto introductory=game_file_read(*directory_,std::string(resource::legacy_save_directories[3])+"Sys.sav",size,size);
        if(!introductory){if(bytes && !root){progress::State::decode(*bytes);directory_->replace("Sys.sav",*bytes);}return bytes;}
        auto merged=bytes?progress::State::decode(*bytes):progress::State{};const auto imported=progress::State::decode(*introductory);
        const bool changed=merged.merge_introductory(imported);if(root && !changed)return bytes;
        auto result=merged.encode();directory_->replace("Sys.sav",result);return result;
    }
    void write(progress::Block block,std::span<const std::uint8_t> bytes)override{
        write_range(block,bytes,{0,bytes.size()});
    }
    void write_range(progress::Block block,std::span<const std::uint8_t> bytes,progress::DirtyRange range)override{
        const auto at=progress::offset(block);if(bytes.size()!=progress::size(block) || range.begin>=range.end || range.end>bytes.size())throw progress::Error("Invalid progress write block/range length");
        constexpr auto size=progress::layout::system_save_bytes;auto source=directory_->open("Sys.sav");
        if(!source){progress::Bytes initial(size);std::copy(bytes.begin(),bytes.end(),initial.begin()+at);directory_->replace("Sys.sav",initial);return;}
        if(source->size()!=size)throw progress::Error("Unsupported Sys.sav length");source.reset();directory_->update("Sys.sav",size,at+range.begin,bytes.subspan(range.begin,range.end-range.begin));
    }
};
}
std::shared_ptr<progress::Storage> progress_storage(std::shared_ptr<GameDirectory> directory,bool combined){return std::make_shared<ProgressFile>(std::move(directory),combined);}
std::shared_ptr<progress::Storage> progress_storage(const std::filesystem::path& directory){return progress_storage(game_directory(directory));}
}
