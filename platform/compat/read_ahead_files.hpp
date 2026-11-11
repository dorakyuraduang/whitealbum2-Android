#pragma once
#include <winrecomp/readonly_files.hpp>
#include <span>

namespace wa2::platform::compat {
// Optional OS capability. The common caller selects read-only files; adapters
// do not choose save slots or parse saves. Any unused descriptors belong to
// the replacement opener and must close when that callable is destroyed.
struct ReadAheadFile {std::string path;wr::ReadOnlyFileInfo* info;};
class ReadAheadFiles {
public:
    static constexpr std::size_t limit=16;
    virtual ~ReadAheadFiles()=default;
    virtual std::size_t prepare_reads(std::span<ReadAheadFile>)=0;
};
}
