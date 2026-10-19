#include "android_host.hpp"
#include "frame_metrics.hpp"
#include "resource_jni.hpp"
#include "widgets_jni.hpp"
#include "shell_jni.hpp"
#include <SDL_system.h>
#include <atomic>
#include <chrono>
#include <jni.h>

namespace {
wa2::android::FrameMetrics metrics;
std::atomic<bool> reset_requested{true};
jclass overlay_class{};
jmethodID report_method{};
}
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm,void*){
    JNIEnv* env{};
    if(vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK)return JNI_ERR;
    const auto local=env->FindClass("org/wa2/port/FrameHud");
    if(!local)return JNI_ERR;
    overlay_class=static_cast<jclass>(env->NewGlobalRef(local));env->DeleteLocalRef(local);
    if(!overlay_class)return JNI_ERR;
    report_method=env->GetStaticMethodID(overlay_class,"report","(FF)V");
    if(!report_method){env->DeleteGlobalRef(overlay_class);overlay_class=nullptr;return JNI_ERR;}
    if(!wa2::android::initialize_resource_jni(env)){
        env->DeleteGlobalRef(overlay_class);overlay_class=nullptr;report_method=nullptr;return JNI_ERR;
    }
    if(!wa2::android::initialize_widgets_jni(env) || !wa2::android::initialize_shell_jni(env)){
        wa2::android::release_shell_jni(env);wa2::android::release_widgets_jni(env);wa2::android::release_resource_jni(env);
        env->DeleteGlobalRef(overlay_class);overlay_class=nullptr;report_method=nullptr;return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}
extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm,void*){
    JNIEnv* env{};
    if(vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)==JNI_OK){
        wa2::android::release_resource_jni(env);
        wa2::android::release_shell_jni(env);
        wa2::android::release_widgets_jni(env);
        if(overlay_class)env->DeleteGlobalRef(overlay_class);
    }
    overlay_class=nullptr;report_method=nullptr;
}
extern "C" JNIEXPORT void JNICALL Java_org_wa2_port_Wa2Activity_nativeResetFrameMetrics(JNIEnv*,jclass){
    reset_requested.store(true,std::memory_order_release);
}
namespace wa2::android {
void frame_presented() noexcept {
    if(reset_requested.exchange(false,std::memory_order_acq_rel))metrics.reset();
    const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const auto sample=metrics.presented(now);
    if(!sample || !overlay_class || !report_method)return;
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if(!env)return;
    env->CallStaticVoidMethod(overlay_class,report_method,jfloat(sample->fps),jfloat(sample->average_ms));
    // An optional counter must not terminate gameplay if its Java view closes.
    if(env->ExceptionCheck())env->ExceptionClear();
}
}
