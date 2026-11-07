#include "user_menu_jni.hpp"
#include "../../platform/native_user_menu.hpp"
#include "../../platform/sdl_user_command.hpp"
#include <SDL.h>

namespace {
wa2::platform::NativeUserMenu menu;
jclass menu_class{};bool registered{};
jobjectArray labels(JNIEnv* env,jclass){
    const auto type=env->FindClass("java/lang/String");if(!type)return nullptr;const auto& values=wa2::game::user_command_labels;
    const auto result=env->NewObjectArray(jsize(values.size()),type,nullptr);env->DeleteLocalRef(type);if(!result)return nullptr;
    for(jsize n=0;n<jsize(values.size());++n){const auto label=env->NewStringUTF(values[unsigned(n)].data());if(!label)return nullptr;env->SetObjectArrayElement(result,n,label);env->DeleteLocalRef(label);if(env->ExceptionCheck())return nullptr;}return result;
}
jlong state(JNIEnv*,jclass){return jlong(menu.state());}
jboolean choose(JNIEnv* env,jclass,jint index){
    if(index<0 || unsigned(index)>=unsigned(wa2::game::UserCommand::count)){const auto type=env->FindClass("java/lang/IllegalArgumentException");if(type){env->ThrowNew(type,"Unknown native user command");env->DeleteLocalRef(type);}return JNI_FALSE;}
    const auto command=wa2::game::UserCommand(index);if(!(std::uint32_t(menu.state())&wa2::game::command_bit(command)) || !(SDL_WasInit(SDL_INIT_EVENTS)&SDL_INIT_EVENTS))return JNI_FALSE;
    auto event=wa2::platform::user_command_event(command);return SDL_PushEvent(&event)==1?JNI_TRUE:JNI_FALSE;
}
}
namespace wa2::android {
void publish_user_menu(game::UserMenuState value)noexcept{menu.publish(value);}
bool initialize_user_menu_jni(JNIEnv* env)noexcept{
    if(!env || menu_class || registered)return false;menu.publish({});const auto local=env->FindClass("org/wa2/port/PortUi");if(!local)return false;
    menu_class=static_cast<jclass>(env->NewGlobalRef(local));env->DeleteLocalRef(local);if(!menu_class)return false;
    const JNINativeMethod methods[]{{"nativeLabels","()[Ljava/lang/String;",reinterpret_cast<void*>(labels)},{"nativeState","()J",reinterpret_cast<void*>(state)},{"nativeChoose","(I)Z",reinterpret_cast<void*>(choose)}};
    registered=env->RegisterNatives(menu_class,methods,3)==JNI_OK;return registered && !env->ExceptionCheck();
}
void release_user_menu_jni(JNIEnv* env)noexcept{menu.publish({});if(menu_class){if(registered)env->UnregisterNatives(menu_class);env->DeleteGlobalRef(menu_class);}menu_class=nullptr;registered=false;}
}
