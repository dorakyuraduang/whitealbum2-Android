// Never loaded or called by the verifier. These exports make the linker resolve
// the full native scene and storage adapter, including methods not used by the
// synthetic progress test. They are not the released application's ABI.
#include "scene.hpp"
#include "progress_storage.hpp"
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
extern "C" WA2_LINK_CHECK std::uint32_t wa2_native_link_frame(void* pointer,std::int64_t now){return static_cast<wa2::game::Scene*>(pointer)->advance_frame(now,{},[now]{return now;}).ticks;}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_script_state(wa2::script::Session* session,const std::uint8_t* data,std::size_t size){
    if(data)session->restore({data,size});return session->checkpoint().size();
}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_dialogue_state(wa2::script::SceneDialogue* dialogue,const std::uint8_t* data,std::size_t size){
    if(data)dialogue->restore({data,size});return dialogue->checkpoint().size();
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
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_image_state(wa2::resource::Catalog* resources,const std::uint8_t* data,std::size_t size){
    auto reference=wa2::image::restore_reference({data,size});auto request=resources->image(reference);return wa2::image::checkpoint_reference(resources->reference(request)).size();
}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_animation(const std::uint8_t* data,std::size_t size){
    auto definition=wa2::image::Animation::parse({data,size});wa2::image::AnimationPlayer player(definition);player.tick();return player.checkpoint().size();
}
extern "C" WA2_LINK_CHECK bool wa2_native_link_geometry(const wa2::image::QuadGeometry* geometry,std::uint32_t width,std::uint32_t height){
    if(!geometry)throw std::invalid_argument("Missing link-check geometry");return wa2::image::scene_quad(*geometry,width,height).has_value();
}
