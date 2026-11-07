#pragma once
#include <jni.h>

namespace wa2::android {
bool initialize_frame_hud_jni(JNIEnv*)noexcept;
void release_frame_hud_jni(JNIEnv*)noexcept;
}
