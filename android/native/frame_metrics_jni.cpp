#include "frame_hud_jni.hpp"
#include "resource_jni.hpp"
#include "widgets_jni.hpp"
#include "shell_jni.hpp"
#include "service_lifecycle.hpp"
#include "subtitles_android.hpp"
#include <array>

namespace {
constexpr std::array<wa2::android::JniService<JNIEnv>,5> services{{
    {wa2::android::initialize_frame_hud_jni,wa2::android::release_frame_hud_jni},
    {wa2::android::initialize_resource_jni,wa2::android::release_resource_jni},
    {wa2::android::initialize_widgets_jni,wa2::android::release_widgets_jni},
    {wa2::android::initialize_shell_jni,wa2::android::release_shell_jni},
    {wa2::android::initialize_subtitles_jni,wa2::android::release_subtitles_jni}
}};
}
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm,void*){
    JNIEnv* env{};if(!vm || vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK)return JNI_ERR;
    return wa2::android::initialize_services<JNIEnv>(env,services)?JNI_VERSION_1_6:JNI_ERR;
}
extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm,void*){
    JNIEnv* env{};if(vm && vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)==JNI_OK)wa2::android::release_services<JNIEnv>(env,services);
}
