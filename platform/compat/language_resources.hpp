#pragma once
#include "../../engine/language_resources.hpp"
#include "../../engine/resource_archive.hpp"
#include <filesystem>
namespace wr {class Process;}
namespace wa2::platform::compat {
// Install before image load. Only archive names/metadata are redirected; no
// resource is extracted, rewritten, copied to app storage or kept as a blob.
void select_language_resources(wr::Process&,const resource::LanguageResources&);
// The English patch contains directory-scoped overrides. Return nullopt for
// ordinary lookup, or an explicit missing member for a handled script bank.
// Archive pointers remain owned by the immutable selected mount.
struct LanguageMember {const resource::Archive* archive;std::size_t member;std::uint64_t identity;};
std::optional<LanguageMember> language_resource_member(wr::Process&,const std::filesystem::path&,std::string_view);
}
