#include "options_audio.hpp"

namespace wa2::game {
void options_preview_effects(script::SceneEffects& effects,const Preferences& preferences){
    if(preferences.audio_muted[0] || preferences.audio_muted[2])return;
    effects.play(15,8101,0,0,255);
}
void options_preview_voice(script::SceneVoices& voices,wa2::Random& random,const Preferences& preferences){
    constexpr std::array<script::SpokenLine,5> samples{{{1,2503,99,false},{2,2002,242,false},{3,2027,223,false},{4,2001,86,false},{5,2001,198,false}}};
    const auto sample=samples[random.next()%samples.size()];if(!preferences.audio_muted[0])voices.preview(sample);
}
void options_reset_sound(script::SceneEffects& effects){effects.once(8905,255);}
}
