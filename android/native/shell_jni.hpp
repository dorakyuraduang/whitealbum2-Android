#pragma once
#include <jni.h>
namespace wa2::android {bool initialize_shell_jni(JNIEnv*) noexcept;void release_shell_jni(JNIEnv*) noexcept;}
