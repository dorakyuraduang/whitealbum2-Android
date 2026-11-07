#pragma once
#include "save_slots.hpp"
#include "resource_archive.hpp"
#include "legacy_save_path.hpp"
#include <functional>

namespace wa2::game {
struct LegacySaveFile {std::string name;std::uint64_t bytes{},modified_ms{};std::string diagnostic;};
struct LegacySaveSources {
    std::function<std::vector<LegacySaveFile>()> list;
    std::function<std::shared_ptr<resource::Source>(std::string_view)> open;
};
// Existing native slots, including damaged ones, take precedence. Replacements
// always go to native storage. The source files are never written or copied.
std::shared_ptr<SaveStorage> legacy_save_storage(std::shared_ptr<SaveStorage>,LegacySaveSources,bool combined);
}
