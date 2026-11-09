#pragma once
#include "../../engine/external_subtitles.hpp"
#include <jni.h>
namespace wa2::android {
bool initialize_subtitles_jni(JNIEnv*)noexcept;
void release_subtitles_jni(JNIEnv*)noexcept;
std::shared_ptr<const subtitles::Catalogue> selected_subtitles();
}
