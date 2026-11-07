#include "frame_hud_jni.hpp"
#include "resource_jni.hpp"
#include "service_lifecycle.hpp"
#include <array>

namespace {
// Native menus belong to the common engine. No Win32 widget, shell, compiled
// resource-image or AOT process adapter participates in this library.
constexpr std::array<wa2::android::JniService<JNIEnv>,2> services{{
    {wa2::android::initialize_frame_hud_jni,wa2::android::release_frame_hud_jni},
    {wa2::android::initialize_resource_jni,wa2::android::release_resource_jni}
}};
}
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm,void*){
    JNIEnv* env{};if(!vm || vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK)return JNI_ERR;
    return wa2::android::initialize_services<JNIEnv>(env,services)?JNI_VERSION_1_6:JNI_ERR;
}
extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm,void*){
    JNIEnv* env{};if(vm && vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)==JNI_OK)wa2::android::release_services<JNIEnv>(env,services);
}
