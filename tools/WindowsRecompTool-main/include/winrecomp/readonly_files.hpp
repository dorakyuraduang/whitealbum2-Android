#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace wr {
struct ReadOnlyFileInfo {
    std::string name;std::uint64_t size{},modified_100ns{};std::uint32_t attributes{0x21};
    // Optional already-resolved address. Each invocation opens a NEW read-only
    // stream; it never retains file contents or a descriptor. Mutable mounts
    // may attach this to operation/update-scoped metadata to avoid resolving
    // the same document path twice. Writers must use the normal open path.
    std::function<std::FILE*(std::uint32_t& error)> open_read;
};
// Logical resource filenames are independent of the host's filesystem path.
// open() transfers one ordinary seekable FILE ownership to its caller. The
// mount contains file handles/metadata, never an extracted archive's payload.
class ReadOnlyFiles {
public:
    virtual ~ReadOnlyFiles()=default;
    virtual std::optional<ReadOnlyFileInfo> info(std::string_view logical) const=0;
    virtual std::vector<ReadOnlyFileInfo> list(std::string_view directory) const=0;
    virtual std::FILE* open(std::string_view logical)=0;
};
// A separately mounted writable namespace. Streams are seekable ordinary file
// descriptors; resources remain in ReadOnlyFiles. Errors use Win32 codes so
// providers do not silently fall back to an unrelated private directory.
class MutableFiles {
public:
    // One CreateFile operation owns this metadata/alias decision. It expires
    // before the next API call, so external edits are never hidden by a cache.
    struct OpenPlan {
        std::optional<ReadOnlyFileInfo> info;
        std::string identity;
        std::function<std::FILE*(bool,bool,std::uint32_t,std::uint32_t&)> open;
    };
    virtual ~MutableFiles()=default;
    virtual std::optional<ReadOnlyFileInfo> info(std::string_view)=0;
    virtual std::string identity(std::string_view name){return std::string(name);}
    virtual std::vector<ReadOnlyFileInfo> list(std::string_view)=0;
    virtual std::FILE* open(std::string_view,bool read,bool write,std::uint32_t creation,std::uint32_t& error)=0;
    virtual OpenPlan prepare_open(std::string_view name){
        return {info(name),identity(name),[this,path=std::string(name)](bool read,bool write,std::uint32_t creation,std::uint32_t& error){return open(path,read,write,creation,error);}};
    }
    virtual std::uint32_t directory(std::string_view)=0;
    virtual std::uint32_t remove(std::string_view,bool directory)=0;
    virtual std::uint32_t move(std::string_view,std::string_view)=0;
    virtual std::uint32_t attributes(std::string_view,std::uint32_t)=0;
    virtual void replace(std::string_view,std::span<const std::uint8_t>)=0;
};
}
