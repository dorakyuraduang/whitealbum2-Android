#include "resource_fd.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <limits>
#include <mutex>
#include <utility>
#include <sys/stat.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace wa2::platform {
namespace {
void close_fd(int fd) noexcept {
    if(fd<0)return;
#ifdef _WIN32
    _close(fd);
#else
    // Do not retry close(EINTR): on Android/Linux the fd is already released.
    close(fd);
#endif
}
struct Descriptor {
    int value{-1};
    explicit Descriptor(int fd) noexcept:value(fd){}
    Descriptor(Descriptor&& other) noexcept:value(std::exchange(other.value,-1)){}
    Descriptor(const Descriptor&)=delete;
    Descriptor& operator=(const Descriptor&)=delete;
    ~Descriptor(){close_fd(value);}
};
class ResourceDescriptor final : public resource::Source {
    Descriptor fd_;
    std::uint64_t bytes_{};
#ifdef _WIN32
    std::mutex mutex_;
#endif
public:
    explicit ResourceDescriptor(Descriptor fd):fd_(std::move(fd)) {
        if(fd_.value<0)throw resource::Error("missing resource file descriptor");
#ifdef _WIN32
        struct _stat64 info{};
        if(_fstat64(fd_.value,&info)!=0)throw resource::Error("cannot stat resource descriptor");
        if((info.st_mode&_S_IFMT)!=_S_IFREG || info.st_size<0 ||
           _lseeki64(fd_.value,0,SEEK_CUR)<0)
            throw resource::Error("resource must be a seekable local file");
        if(_setmode(fd_.value,_O_BINARY)<0)throw resource::Error("cannot set binary resource mode");
#else
        static_assert(sizeof(off_t)>=8);
        struct stat info{};
        if(fstat(fd_.value,&info)!=0)throw resource::Error("cannot stat resource descriptor");
        if(!S_ISREG(info.st_mode) || info.st_size<0 || lseek(fd_.value,0,SEEK_CUR)<0)
            throw resource::Error("resource must be a seekable local file");
#endif
        bytes_=std::uint64_t(info.st_size);
    }
    std::uint64_t size() const override {return bytes_;}
    void read_at(std::uint64_t offset,std::span<std::uint8_t> bytes) override {
        if(offset>bytes_ || bytes.size()>bytes_-offset ||
           offset>std::uint64_t(std::numeric_limits<std::int64_t>::max()))
            throw resource::Error("resource descriptor read outside file");
        if(bytes.empty())return;
#ifdef _WIN32
        // The Windows CRT fallback is only for portable-module verification.
        // Android/Linux use positional reads without this cursor or mutex.
        std::lock_guard lock(mutex_);
        if(_lseeki64(fd_.value,std::int64_t(offset),SEEK_SET)<0)
            throw resource::Error("cannot seek resource descriptor");
#endif
        while(!bytes.empty()) {
            const auto count=std::min<std::size_t>(bytes.size(),1024u*1024);
#ifdef _WIN32
            const auto got=_read(fd_.value,bytes.data(),unsigned(count));
#else
            const auto got=pread(fd_.value,bytes.data(),count,off_t(offset));
#endif
            if(got<0 && errno==EINTR)continue;
            if(got<0)throw resource::Error("cannot read resource descriptor");
            if(got==0)throw resource::Error("short resource descriptor read");
            offset+=std::uint64_t(got);bytes=bytes.subspan(std::size_t(got));
        }
    }
};
}
std::shared_ptr<resource::Source> adopt_resource_fd(int descriptor) {
    Descriptor owned(descriptor);
    return std::make_shared<ResourceDescriptor>(std::move(owned));
}
}
