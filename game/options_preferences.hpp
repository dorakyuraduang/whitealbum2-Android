#pragma once
#include "options_view.hpp"
#include "preferences.hpp"

namespace wa2::game {
// The common view uses atlas-cell indices; the engine uses named policies and
// gains. Conversion preserves fields which are not edited by these three pages.
OptionsState options_state(const Preferences&);
Preferences options_preferences(const OptionsState&,const Preferences& previous);
OptionsTiming options_timing(const Preferences&);
Preferences options_defaults(const Preferences& previous);
}
