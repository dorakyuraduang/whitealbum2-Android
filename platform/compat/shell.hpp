#pragma once
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace wr {class Process;}
namespace wa2::platform::compat {
struct FileFilter {std::u16string label,pattern;};
struct FileRequest {
    bool save{},directory{};std::uint32_t owner{},flags{};
    std::filesystem::path initial;std::u16string title,extension,suggested_filename;
    std::vector<FileFilter> filters;
};
struct ShellServices {
    std::function<std::optional<std::filesystem::path>(const FileRequest&)> pick;
    std::function<bool(std::u16string_view target,std::u16string_view verb)> open;
};
ShellServices native_shell_services(const std::filesystem::path& private_root);
void install_shell(wr::Process&,ShellServices);
}
