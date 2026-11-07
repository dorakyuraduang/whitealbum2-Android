#pragma once
#include "scene_effects.hpp"
#include "scene_voices.hpp"
#include "native_random.hpp"
#include "preferences.hpp"

namespace wa2::game {
// Same source channels and samples from title and story settings.
void options_preview_effects(script::SceneEffects&,const Preferences&);
void options_preview_voice(script::SceneVoices&,wa2::Random&,const Preferences&);
void options_reset_sound(script::SceneEffects&);
}
