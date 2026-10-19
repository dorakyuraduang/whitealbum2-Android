#pragma once
#include <jni.h>
namespace wa2::android {
bool initialize_widgets_jni(JNIEnv*) noexcept;
void release_widgets_jni(JNIEnv*) noexcept;
}
