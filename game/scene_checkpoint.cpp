#include "scene.hpp"
#include "saved_scene.hpp"

namespace wa2::game {
std::vector<std::uint8_t> Scene::checkpoint()const{
    if(title_requested_ || !session_.active() || movies_.playback())throw script::Unsupported("Cannot save an inactive scene or a movie transport");
    SavedScene value;value.logical_us=timeline_.microseconds();value.random=random_.state();value.command=statistics_.command;value.ticks=statistics_.ticks;value.messages=statistics_.messages;value.voices=statistics_.voices;value.scene=session_.active()->program().name();value.background_changing=background_changing_;value.preserve_characters=preserve_characters_;value.window_visible=window_visible_;value.skip_read_only=skip_read_only_;value.replay=replay_mode_;
    value.part(ScenePart::script)=session_.checkpoint();value.part(ScenePart::reading)=reading_.checkpoint();value.part(ScenePart::controls)=controls_.checkpoint();value.part(ScenePart::choices)=choices_.checkpoint();value.part(ScenePart::selection)=selection_.checkpoint(choices_,resources_);value.part(ScenePart::calendar)=calendar_.checkpoint();value.part(ScenePart::shake)=shake_.checkpoint();value.part(ScenePart::weather)=weather_.checkpoint(resources_);value.part(ScenePart::background)=backgrounds_.checkpoint(resources_);value.part(ScenePart::bitmaps)=bitmaps_.checkpoint(resources_);value.part(ScenePart::characters)=characters_.checkpoint(resources_);value.part(ScenePart::colour)=colour_.checkpoint();value.part(ScenePart::dialogue)=dialogue_.checkpoint();value.part(ScenePart::backlog)=backlog_.checkpoint();value.part(ScenePart::music)=music_.checkpoint();value.part(ScenePart::effects)=effects_.checkpoint();value.part(ScenePart::voices)=voices_.checkpoint();return value.encode();
}
std::unique_ptr<Scene> Scene::restore(resource::BankOpener opener,std::shared_ptr<progress::Store> progress,std::span<const std::uint8_t> bytes,std::int64_t now,const SavedImageResolver& saved_images,const Preferences& preferences){
    const auto value=SavedScene::decode(bytes);preferences.validate();if(now<0 || !saved_images || !progress)throw script::Error("Missing native scene restore clock, resources or progress storage");
    // A fresh owner keeps every callback bound to its final address. Its mixer
    // is not attached to a device: failed loading cannot replace or interrupt
    // the old scene's transports. The application swaps owners only on success.
    auto next=std::make_unique<Scene>(std::move(opener),value.random,std::move(progress));auto resolve=saved_images(next->resources_);if(!resolve)throw script::Error("Missing native scene saved-image resolver");
    next->timeline_.rebase(value.logical_us,now);next->milliseconds_=next->timeline_.milliseconds();next->statistics_={value.ticks,value.messages,value.voices,value.command,value.scene};next->background_changing_=value.background_changing;next->preserve_characters_=value.preserve_characters;next->window_visible_=value.window_visible;next->skip_read_only_=value.skip_read_only;next->replay_mode_=value.replay;
    next->session_.restore(value.part(ScenePart::script));if(!next->session_.active() || next->session_.active()->program().name()!=value.scene)throw script::Error("Saved scene metadata does not match its script stack");next->library_.functions().at(value.command);if(next->session_.active()->pending_command()==193)throw script::Unsupported("Movie transport is not part of a native scene save");
    next->reading_.restore(value.part(ScenePart::reading));next->controls_.restore(value.part(ScenePart::controls));next->controls_.latched_skip(false);next->choices_.restore(value.part(ScenePart::choices));next->calendar_.restore(value.part(ScenePart::calendar));next->shake_.restore(value.part(ScenePart::shake));next->weather_.restore(value.part(ScenePart::weather),next->resources_,resolve);next->backgrounds_.restore(value.part(ScenePart::background),next->resources_,resolve);next->bitmaps_.restore(value.part(ScenePart::bitmaps),next->resources_,resolve);next->characters_.restore(value.part(ScenePart::characters),next->resources_,resolve);next->colour_.restore(value.part(ScenePart::colour));next->dialogue_.restore(value.part(ScenePart::dialogue));next->backlog_.restore(value.part(ScenePart::backlog));next->selection_.restore(value.part(ScenePart::selection),next->choices_,next->resources_,resolve);
    // Scene snapshots retain their own UI state for validation, but current
    // application preferences win before loading UI images or creating audio.
    next->preferences(preferences);next->selection_.sync(next->choices_);
    next->calendar_.prepare_restored_assets(next->resources_,resolve);next->dialogue_.prepare_restored_assets(next->resources_,resolve);
    next->captions_.reset();next->voices_.adult_mode(next->controls_.modes().adult_mode!=0);next->music_.restore(value.part(ScenePart::music));next->effects_.restore(value.part(ScenePart::effects));next->voices_.restore(value.part(ScenePart::voices));return next;
}
}
