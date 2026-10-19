#include "compiled_resources.hpp"
#include "resource_jni.hpp"
#include "../../platform/compat/resource_mount.hpp"
#include "../../platform/clock.hpp"
#include <cerrno>
#include <cstdio>
#include <limits>
#include <unistd.h>
namespace wa2::android {
void mount_compiled_resources(wr::Process& process){
    std::vector<wr::ReadOnlyFileInfo> files;
    for(auto& record:resource_metadata()){
        if(record.modified_millis>(std::numeric_limits<std::uint64_t>::max()-116444736000000000ull)/10000)
            throw std::runtime_error("Android archive timestamp exceeds source FILETIME range");
        files.push_back({std::move(record.name),record.size,record.modified_millis?record.modified_millis*10000+116444736000000000ull:0});
    }
    auto mount=std::make_shared<platform::compat::ResourceMount>(std::move(files),[](std::string_view name)->std::FILE*{
        const auto descriptor=open_resource_descriptor(name);auto* file=fdopen(descriptor,"rb");if(!file){const auto saved=errno;close(descriptor);errno=saved;}return file;
    });
    platform::compat::install_resource_mount(process,std::move(mount));
}
}
