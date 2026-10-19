#pragma once
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace wr {
struct ReadOnlyFileInfo {
    std::string name;std::uint64_t size{},modified_100ns{};std::uint32_t attributes{0x21};
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
}
