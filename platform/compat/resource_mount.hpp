#pragma once
#include <winrecomp/readonly_files.hpp>
#include "../../engine/resource_archive.hpp"
#include <filesystem>
#include <functional>
#include <map>
namespace wr {class Process;}
namespace wa2::platform::compat {
std::shared_ptr<wr::ReadOnlyFiles> local_resource_mount(const std::filesystem::path& root);
class ResourceMount final:public wr::ReadOnlyFiles {
    std::map<std::string,wr::ReadOnlyFileInfo> files_;
    std::function<std::FILE*(std::string_view)> open_;
public:
    ResourceMount(std::vector<wr::ReadOnlyFileInfo>,std::function<std::FILE*(std::string_view)>);
    std::optional<wr::ReadOnlyFileInfo> info(std::string_view) const override;
    std::vector<wr::ReadOnlyFileInfo> list(std::string_view) const override;
    std::FILE* open(std::string_view) override;
};
void install_resource_mount(wr::Process&,std::shared_ptr<wr::ReadOnlyFiles>);
std::shared_ptr<resource::Source> open_process_resource(wr::Process&,const std::filesystem::path&);
std::optional<wr::ReadOnlyFileInfo> mounted_resource_info(wr::Process&,const std::filesystem::path&);
}
