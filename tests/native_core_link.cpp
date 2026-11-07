// Never loaded or called by the verifier. These exports make the linker resolve
// the full native scene and storage adapter, including methods not used by the
// synthetic progress test. They are not the released application's ABI.
#include "scene.hpp"
#include "save_view.hpp"
#include "application.hpp"
#include "title_view.hpp"
#include "options_view.hpp"
#include "options_preferences.hpp"
#include "render_frame.hpp"
#include "frame_driver.hpp"
#include "audio_binding.hpp"
#include "native_application.hpp"
#include "progress_storage.hpp"
#include "save_storage.hpp"
#include "preference_storage.hpp"
#include "sprite_animation.hpp"
#include "scene_geometry.hpp"
#if defined(_WIN32)
#define WA2_LINK_CHECK __declspec(dllexport)
#else
#define WA2_LINK_CHECK __attribute__((visibility("default")))
#endif
extern "C" WA2_LINK_CHECK void* wa2_native_link_create(wa2::resource::BankOpener* opener,const std::filesystem::path* directory){
    if(!opener || !directory)throw std::invalid_argument("Missing link-check factory arguments");
    auto progress=std::make_shared<wa2::progress::Store>(wa2::platform::progress_storage(*directory));
    return new wa2::game::Scene(*opener,1,std::move(progress));
}
extern "C" WA2_LINK_CHECK void wa2_native_link_destroy(void* pointer){delete static_cast<wa2::game::Scene*>(pointer);}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_tick(void* pointer,std::int64_t clock){
    auto& scene=*static_cast<wa2::game::Scene*>(pointer);scene.update(clock);
    scene.background();scene.movie();scene.pause(false,clock);scene.stop_movie();scene.dialogue_transform();
    return scene.environment().size()+scene.foreground().size()+scene.dialogue().size()+scene.calendar().size()+scene.captions().size()+scene.selection().size();
}
extern "C" WA2_LINK_CHECK void wa2_native_link_start(void* pointer){static_cast<wa2::game::Scene*>(pointer)->start();}
extern "C" WA2_LINK_CHECK bool wa2_native_link_preferences(void* pointer,wa2::game::PreferenceStorage* storage,const wa2::game::Preferences* value){wa2::game::PreferenceStore preferences(std::shared_ptr<wa2::game::PreferenceStorage>(storage,[](auto*){}));if(value)preferences.replace(*value);static_cast<wa2::game::Scene*>(pointer)->preferences(preferences.current());return value && preferences.current()==*value;}
extern "C" WA2_LINK_CHECK bool wa2_native_link_preference_file(const std::filesystem::path* directory,const wa2::game::Preferences* value){wa2::game::PreferenceStore settings(wa2::platform::preference_storage(*directory));return value && settings.replace(*value);}
extern "C" WA2_LINK_CHECK void* wa2_native_link_application_create(wa2::resource::BankOpener* opener,const std::filesystem::path* directory,const wa2::game::Scene::SavedImageResolver* resolver){return new wa2::game::Application(*opener,std::make_shared<wa2::progress::Store>(wa2::platform::progress_storage(*directory)),wa2::platform::preference_storage(*directory),wa2::platform::save_storage(*directory),*resolver);}
extern "C" WA2_LINK_CHECK unsigned wa2_native_link_application_update(void* pointer,std::int64_t now,unsigned mode,unsigned slot){auto& app=*static_cast<wa2::game::Application*>(pointer);switch(mode){case 0:app.start({},now);break;case 1:app.open(wa2::game::Page::save,now);app.save(slot);break;case 2:app.load(slot,now);break;case 3:app.background(true,now);break;case 4:app.background(false,now);app.back(now);break;case 5:app.title(now);break;case 6:app.close(now);break;default:throw std::invalid_argument("Unsupported application link-check operation");}if(app.page()!=wa2::game::Page::closed)app.frame(now,{},[now]{return now;});return unsigned(app.page());}
extern "C" WA2_LINK_CHECK void wa2_native_link_application_destroy(void* pointer){delete static_cast<wa2::game::Application*>(pointer);}
extern "C" WA2_LINK_CHECK void* wa2_native_link_application_bootstrap(wa2::resource::BankOpener* opener,const std::filesystem::path* directory){return wa2::platform::native_application(*opener,*directory).release();}
extern "C" WA2_LINK_CHECK void* wa2_native_link_driver_create(void* application){return new wa2::game::FrameDriver(*static_cast<wa2::game::Application*>(application));}
extern "C" WA2_LINK_CHECK unsigned wa2_native_link_driver_pump(void* pointer,std::int64_t now){return static_cast<wa2::game::FrameDriver*>(pointer)->pump(now,{},[now]{return now;}).updates;}
extern "C" WA2_LINK_CHECK void wa2_native_link_driver_destroy(void* pointer){delete static_cast<wa2::game::FrameDriver*>(pointer);}
extern "C" WA2_LINK_CHECK void* wa2_native_link_audio_binding_create(wa2::platform::AudioOutputFactory* factory){return new wa2::platform::AudioBinding(*factory);}
extern "C" WA2_LINK_CHECK void wa2_native_link_audio_binding_update(void* pointer,std::shared_ptr<wa2::audio::Mixer>* mixer,bool pause){static_cast<wa2::platform::AudioBinding*>(pointer)->synchronize(mixer?*mixer:nullptr,pause);}
extern "C" WA2_LINK_CHECK void wa2_native_link_audio_binding_destroy(void* pointer){delete static_cast<wa2::platform::AudioBinding*>(pointer);}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_render_frame(void* pointer){wa2::game::RenderFrame frame;frame.capture(*static_cast<wa2::game::Application*>(pointer));return frame.environment().size()+frame.overlays().size()+frame.movie().has_value();}
extern "C" WA2_LINK_CHECK void* wa2_native_link_title_create(wa2::game::TitleView::Loader* loader,wa2::game::TitleView::Random* random,const wa2::progress::State* progress){auto view=std::make_unique<wa2::game::TitleView>(*loader,*random);view->prepare({true,false,true},*progress);return view.release();}
extern "C" WA2_LINK_CHECK unsigned wa2_native_link_title_update(void* pointer,const wa2::progress::State* progress){auto& view=*static_cast<wa2::game::TitleView*>(pointer);view.update();if(auto command=view.take_command();command && view.phase()==wa2::game::TitlePhase::modal)view.resume(*progress);return unsigned(view.sprites().size());}
extern "C" WA2_LINK_CHECK void wa2_native_link_title_destroy(void* pointer){delete static_cast<wa2::game::TitleView*>(pointer);}
extern "C" WA2_LINK_CHECK void* wa2_native_link_options_create(wa2::game::OptionsView::Loader* loader,const wa2::game::OptionsState* state){auto view=std::make_unique<wa2::game::OptionsView>(*loader);view->prepare(*state);return view.release();}
extern "C" WA2_LINK_CHECK unsigned wa2_native_link_options_update(void* pointer){auto& view=*static_cast<wa2::game::OptionsView*>(pointer);view.update();return unsigned(view.sprites().size());}
extern "C" WA2_LINK_CHECK void wa2_native_link_options_destroy(void* pointer){delete static_cast<wa2::game::OptionsView*>(pointer);}
extern "C" WA2_LINK_CHECK unsigned wa2_native_link_options_preferences(wa2::game::Preferences* preferences,const wa2::game::OptionsState* state){if(state)*preferences=wa2::game::options_preferences(*state,*preferences);return unsigned(wa2::game::options_state(*preferences).values.size());}
extern "C" WA2_LINK_CHECK std::uint32_t wa2_native_link_frame(void* pointer,std::int64_t now){return static_cast<wa2::game::Scene*>(pointer)->advance_frame(now,{},[now]{return now;}).ticks;}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_save_scene(void* pointer){return static_cast<wa2::game::Scene*>(pointer)->checkpoint().size();}
extern "C" WA2_LINK_CHECK void* wa2_native_link_restore_scene(wa2::resource::BankOpener* opener,std::shared_ptr<wa2::progress::Store>* progress,const std::uint8_t* data,std::size_t size,std::int64_t now,const wa2::game::Scene::SavedImageResolver* resolve){return wa2::game::Scene::restore(*opener,*progress,{data,size},now,*resolve).release();}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_save_slot(void* pointer,const std::filesystem::path* directory,unsigned slot){wa2::game::SaveSlots saves(wa2::platform::save_storage(*directory));saves.write(slot,static_cast<wa2::game::Scene*>(pointer)->checkpoint());return saves.read(slot)->size()+saves.list().size();}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_script_state(wa2::script::Session* session,const std::uint8_t* data,std::size_t size){
    if(data)session->restore({data,size});return session->checkpoint().size();
}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_dialogue_state(wa2::script::SceneDialogue* dialogue,const std::uint8_t* data,std::size_t size){
    if(data)dialogue->restore({data,size});return dialogue->checkpoint().size();
}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_save_view(wa2::game::SaveView* view,const wa2::game::SaveInput* input){
    view->update(input?*input:wa2::game::SaveInput{});return view->sprites().size();
}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_backlog_state(wa2::script::SceneBacklog* backlog,const std::uint8_t* data,std::size_t size){
    if(data)backlog->restore({data,size});return backlog->checkpoint().size();
}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_background_state(wa2::script::SceneBackground* background,wa2::resource::Catalog* resources,const wa2::script::SceneBackground::Resolve* resolve,const std::uint8_t* data,std::size_t size){
    if(data)background->restore({data,size},*resources,*resolve);return background->checkpoint(*resources).size();
}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_colour_state(wa2::script::SceneColour* colour,const std::uint8_t* data,std::size_t size){
    if(data)colour->restore({data,size});return colour->checkpoint().size();
}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_character_state(wa2::script::SceneCharacters* characters,wa2::resource::Catalog* resources,const wa2::image::Resolve* resolve,const std::uint8_t* data,std::size_t size){
    if(data)characters->restore({data,size},*resources,*resolve);return characters->checkpoint(*resources).size();
}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_bitmap_state(wa2::script::SceneBitmaps* bitmaps,wa2::resource::Catalog* resources,const wa2::image::Resolve* resolve,const std::uint8_t* data,std::size_t size){
    if(data)bitmaps->restore({data,size},*resources,*resolve);return bitmaps->checkpoint(*resources).size();
}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_weather_state(wa2::script::SceneWeather* weather,wa2::resource::Catalog* resources,const wa2::image::Resolve* resolve,const std::uint8_t* data,std::size_t size){
    if(data)weather->restore({data,size},*resources,*resolve);return weather->checkpoint(*resources).size();
}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_audio_state(wa2::script::SceneMusic* music,wa2::script::SceneEffects* effects,wa2::script::SceneVoices* voices,const std::vector<std::uint8_t>* data){
    if(data){music->restore(data[0]);effects->restore(data[1]);voices->restore(data[2]);}return music->checkpoint().size()+effects->checkpoint().size()+voices->checkpoint().size();
}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_image_state(wa2::resource::Catalog* resources,const std::uint8_t* data,std::size_t size){
    auto reference=wa2::image::restore_reference({data,size});auto request=resources->image(reference);return wa2::image::checkpoint_reference(resources->reference(request)).size();
}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_animation(const std::uint8_t* data,std::size_t size){
    auto definition=wa2::image::Animation::parse({data,size});wa2::image::AnimationPlayer player(definition);player.tick();return player.checkpoint().size();
}
extern "C" WA2_LINK_CHECK bool wa2_native_link_geometry(const wa2::image::QuadGeometry* geometry,std::uint32_t width,std::uint32_t height){
    if(!geometry)throw std::invalid_argument("Missing link-check geometry");return wa2::image::scene_quad(*geometry,width,height).has_value();
}
