#pragma once
#include <winrecomp/readonly_files.hpp>
#include <filesystem>
#include <functional>
namespace wa2::platform::compat {
// All hosts share source-path aliases and test-profile policy; the supplied
// backend implements only OS file operations in the selected directory.
// Normal .sav files always use Documents/Leaf/WHITE ALBUM2; root saves are
// ignored. Test-mode files remain isolated below test-mode/.
using SaveReadScope=std::function<std::optional<std::uint64_t>()>;
using SaveReadAhead=std::function<bool()>;
std::shared_ptr<wr::MutableFiles> save_files(std::shared_ptr<wr::MutableFiles>,bool test_mode,SaveReadScope={},SaveReadAhead={});
std::string save_files_report(const std::shared_ptr<wr::MutableFiles>&);
std::optional<std::uint64_t> save_files_revision(const wr::MutableFiles*);
std::optional<std::uint64_t> save_files_activity(const wr::MutableFiles*);
std::shared_ptr<wr::MutableFiles> local_files(const std::filesystem::path&);
}
