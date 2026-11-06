// Never loaded or called by the verifier. These exports make the linker resolve
// the full native scene and storage adapter, including methods not used by the
// synthetic progress test. They are not the released application's ABI.
#include "scene.hpp"
#include "progress_storage.hpp"
#include "sprite_animation.hpp"
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
    scene.background();scene.movie();scene.pause(false,clock);scene.stop_movie();
    return scene.environment().size()+scene.foreground().size()+scene.dialogue().size()+scene.calendar().size();
}
extern "C" WA2_LINK_CHECK void wa2_native_link_start(void* pointer){static_cast<wa2::game::Scene*>(pointer)->start();}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_script_state(wa2::script::Session* session,const std::uint8_t* data,std::size_t size){
    if(data)session->restore({data,size});return session->checkpoint().size();
}
extern "C" WA2_LINK_CHECK std::size_t wa2_native_link_animation(const std::uint8_t* data,std::size_t size){
    auto definition=wa2::image::Animation::parse({data,size});wa2::image::AnimationPlayer player(definition);player.tick();return player.checkpoint().size();
}
