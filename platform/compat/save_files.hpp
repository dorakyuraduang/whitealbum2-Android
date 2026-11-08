#pragma once
#include <winrecomp/readonly_files.hpp>
#include <filesystem>
namespace wa2::platform::compat {
// All hosts share source-path aliases and test-profile policy; the supplied
// backend implements only OS file operations in the selected directory.
std::shared_ptr<wr::MutableFiles> save_files(std::shared_ptr<wr::MutableFiles>,bool test_mode);
std::shared_ptr<wr::MutableFiles> local_files(const std::filesystem::path&);
}
