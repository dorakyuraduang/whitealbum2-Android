#pragma once
#include "script_session.hpp"

namespace wa2::script {
// Converts the two data blocks appended to an original manual save into the
// ordinary LSSN format. It neither advances a script nor changes the Library's
// globals. Old allocation/callback pointers are never resolved or retained.
struct LegacyScriptImport {
    std::vector<std::uint8_t> checkpoint;
    std::vector<std::string> scenes;
    std::optional<std::uint32_t> pending_command;
    std::size_t consumed{};
};
// Original saves omit the contents of suspended command strings. A scene
// importer must supply a proved semantic replacement for each such argument;
// the generic converter must not silently invent an empty value.
using LegacyResumeText=std::function<std::optional<std::string>(
    const Program&,std::uint32_t command,unsigned argument)>;
LegacyScriptImport import_legacy_script(std::span<const std::uint8_t>,
    const Functions&,const Globals&,const Loader&,LegacyResumeText={});
}
