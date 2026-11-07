#pragma once
#include <jni.h>
#include "../../game/user_command.hpp"

namespace wa2::android {
bool initialize_user_menu_jni(JNIEnv*)noexcept;
void release_user_menu_jni(JNIEnv*)noexcept;
void publish_user_menu(game::UserMenuState)noexcept;
}
