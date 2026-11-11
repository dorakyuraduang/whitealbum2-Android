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
// Optional metadata reuse for known list readers only. File contents are not
// cached; ordinary opens and actual save restoration retain fresh metadata.
using SaveListRead=std::function<bool()>;
using SavePreviewRead=std::function<bool()>;
std::shared_ptr<wr::MutableFiles> save_files(std::shared_ptr<wr::MutableFiles>,bool test_mode,SaveReadScope={},SaveReadAhead={},SaveListRead={},SavePreviewRead={});
std::string save_files_report(const std::shared_ptr<wr::MutableFiles>&);
std::optional<std::uint64_t> save_files_revision(const wr::MutableFiles*);
std::optional<std::uint64_t> save_files_activity(const wr::MutableFiles*);
std::shared_ptr<wr::MutableFiles> local_files(const std::filesystem::path&);
}
