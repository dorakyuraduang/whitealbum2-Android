#include "android_host.hpp"
#include "frame_metrics.hpp"
#include "frame_hud_jni.hpp"
#include <SDL_system.h>
#include <atomic>
#include <chrono>

namespace {
wa2::android::FrameMetrics metrics;
std::atomic<bool> reset_requested{true};
jclass overlay_class{};
jmethodID report_method{};
}
namespace wa2::android {
bool initialize_frame_hud_jni(JNIEnv* env)noexcept{
    if(overlay_class || report_method)return false;
    const auto local=env->FindClass("org/wa2/port/FrameHud");if(!local)return false;
    overlay_class=static_cast<jclass>(env->NewGlobalRef(local));env->DeleteLocalRef(local);if(!overlay_class)return false;
    report_method=env->GetStaticMethodID(overlay_class,"report","(FF)V");if(!report_method)return false;
    reset_requested.store(true,std::memory_order_release);return true;
}
void release_frame_hud_jni(JNIEnv* env)noexcept{
    if(overlay_class)env->DeleteGlobalRef(overlay_class);overlay_class=nullptr;report_method=nullptr;
    reset_requested.store(true,std::memory_order_release);
}
void frame_presented()noexcept{
    if(reset_requested.exchange(false,std::memory_order_acq_rel))metrics.reset();
    const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const auto sample=metrics.presented(now);if(!sample || !overlay_class || !report_method)return;
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());if(!env)return;
    env->CallStaticVoidMethod(overlay_class,report_method,jfloat(sample->fps),jfloat(sample->average_ms));
    // Closing an optional HUD must not terminate the game thread.
    if(env->ExceptionCheck())env->ExceptionClear();
}
}
extern "C" JNIEXPORT void JNICALL Java_org_wa2_port_Wa2Activity_nativeResetFrameMetrics(JNIEnv*,jclass){
    reset_requested.store(true,std::memory_order_release);
}
