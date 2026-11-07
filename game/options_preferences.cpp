#include "options_preferences.hpp"
#include <algorithm>

namespace wa2::game {
namespace {
// Cached source text pacing: integer(2 / {0,.5,1,2}) at 60 Hz;
// zero is its immediate-text mode. The native layout retains source time units.
constexpr std::array<int,4> steps{0,4,2,1};
}
OptionsState options_state(const Preferences& prefs){
    prefs.validate();OptionsState state;
    const auto speed=std::find(steps.begin(),steps.end(),prefs.text_step);
    if(speed==steps.end() || prefs.auto_ticks<60 || prefs.auto_ticks>600)throw script::Unsupported("Native custom text timing has no source settings cell");
    state.value(Option::display,prefs.fullscreen?0:1);state.value(Option::text_speed,int(speed-steps.begin()));
    state.value(Option::skip_policy,prefs.skip_read_only?1:0);state.value(Option::auto_delay,prefs.auto_ticks);state.value(Option::transition_wait,prefs.transition_wait?1:0);
    state.value(Option::window_opacity,prefs.window_alpha);state.value(Option::picture_opacity,prefs.picture_alpha);state.value(Option::novel_opacity,prefs.novel_alpha);state.value(Option::button_opacity,prefs.button_alpha);
    state.value(Option::window_visibility,prefs.window?0:1);state.value(Option::confirmation,prefs.confirmation?0:1);state.value(Option::right_click,prefs.right_click);
    state.value(Option::master_volume,prefs.master);state.value(Option::music_volume,prefs.music);state.value(Option::effects_volume,prefs.effects);state.value(Option::voice_volume,prefs.speech);
    state.value(Option::voice_advance,prefs.cut_voice_on_advance?0:1);state.value(Option::adult_voice,prefs.mute_adult?1:0);
    for(unsigned n=0;n<10;++n)state.value(Option(unsigned(Option::character_0)+n),prefs.character_voice[n]?0:1);
    std::copy(prefs.audio_muted.begin(),prefs.audio_muted.end(),state.muted.begin());state.muted[3]=!prefs.voice_enabled;state.validate();return state;
}
Preferences options_preferences(const OptionsState& state,const Preferences& previous){
    state.validate();previous.validate();auto prefs=previous;
    prefs.fullscreen=state.value(Option::display)==0;prefs.text_step=steps[unsigned(state.value(Option::text_speed))];
    prefs.skip_read_only=state.value(Option::skip_policy)!=0;prefs.auto_ticks=state.value(Option::auto_delay);prefs.transition_wait=state.value(Option::transition_wait)!=0;
    prefs.window_alpha=state.value(Option::window_opacity);prefs.picture_alpha=state.value(Option::picture_opacity);prefs.novel_alpha=state.value(Option::novel_opacity);prefs.button_alpha=state.value(Option::button_opacity);
    prefs.window=state.value(Option::window_visibility)==0;prefs.confirmation=state.value(Option::confirmation)==0;prefs.right_click=state.value(Option::right_click);
    prefs.master=state.value(Option::master_volume);prefs.music=state.value(Option::music_volume);prefs.effects=state.value(Option::effects_volume);prefs.speech=state.value(Option::voice_volume);
    prefs.cut_voice_on_advance=state.value(Option::voice_advance)==0;prefs.mute_adult=state.value(Option::adult_voice)!=0;
    for(unsigned n=0;n<10;++n)prefs.character_voice[n]=state.value(Option(unsigned(Option::character_0)+n))==0;
    std::copy_n(state.muted.begin(),3,prefs.audio_muted.begin());prefs.voice_enabled=!state.muted[3];prefs.validate();return prefs;
}
OptionsTiming options_timing(const Preferences& prefs){prefs.validate();return {unsigned(prefs.transition_frames(15)),unsigned(prefs.transition_frames(7)),unsigned(prefs.transition_frames(15))};}
}
