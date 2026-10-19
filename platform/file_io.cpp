#include "file_io.hpp"
#include <cerrno>
#include <fstream>
#include <limits>
#include <mutex>
#include <system_error>

namespace wa2::platform {
namespace {
class ResourceFile final : public resource::Source {
    std::ifstream file_;
    std::uint64_t bytes_{};
    std::optional<std::uint64_t> position_;
    std::mutex mutex_;
public:
    explicit ResourceFile(const std::filesystem::path& path):file_(path,std::ios::binary) {
        if(!file_){
            const auto failure=errno;const auto utf8=path.generic_u8string();
            const auto message="cannot open resource file: "+std::string(utf8.begin(),utf8.end())+
                (failure?" ("+std::error_code(failure,std::generic_category()).message()+")":"");
            if(failure==ENOENT)throw resource::NotFound(message);
            throw resource::Error(message);
        }
        file_.seekg(0,std::ios::end);const auto end=file_.tellg();
        if(end<0)throw resource::Error("cannot read resource file size");
        bytes_=std::uint64_t(end);position_=bytes_;
    }
    std::uint64_t size() const override {return bytes_;}
    void read_at(std::uint64_t offset,std::span<std::uint8_t> bytes) override {
        if(offset>bytes_ || bytes.size()>bytes_-offset ||
           offset>std::uint64_t(std::numeric_limits<std::streamoff>::max()) ||
           bytes.size()>std::uint64_t(std::numeric_limits<std::streamsize>::max()))
            throw resource::Error("resource read outside file");
        if(bytes.empty())return;
        std::lock_guard lock(mutex_);
        if(!position_ || *position_!=offset){file_.clear();file_.seekg(std::streamoff(offset));}
        if(!file_ || !file_.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()))){
            position_.reset();
            throw resource::Error("short resource read");
        }
        position_=offset+bytes.size();
    }
};
}
std::shared_ptr<resource::Source> open_resource_file(const std::filesystem::path& path) {
    return std::make_shared<ResourceFile>(path);
}
bool seek_stream(std::FILE* stream,std::uint64_t offset) noexcept {
    if(!stream || offset>std::uint64_t(std::numeric_limits<std::int64_t>::max()))return false;
#ifdef _WIN32
    return _fseeki64(stream,std::int64_t(offset),SEEK_SET)==0;
#else
    static_assert(sizeof(off_t)>=8);
    return fseeko(stream,off_t(offset),SEEK_SET)==0;
#endif
}
std::optional<std::uint64_t> stream_position(std::FILE* stream) noexcept {
    if(!stream)return std::nullopt;
#ifdef _WIN32
    const auto position=_ftelli64(stream);
#else
    const auto position=ftello(stream);
#endif
    return position<0 ? std::nullopt : std::optional<std::uint64_t>(std::uint64_t(position));
}
}
