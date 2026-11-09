#pragma once
#include "../../engine/language_resources.hpp"
namespace wr {class Process;}
namespace wa2::platform::compat {
// Install before image load. Only archive names/metadata are redirected; no
// resource is extracted, rewritten, copied to app storage or kept as a blob.
void select_language_resources(wr::Process&,const resource::LanguageResources&);
}
