#include "android_host.hpp"
#include "frame_metrics.hpp"
#include "frame_hud_jni.hpp"
#include <SDL_system.h>
#include <atomic>
#include <chrono>
#include <mutex>

namespace {
wa2::android::FrameMetrics metrics;
std::atomic<bool> reset_requested{true};
std::atomic<void(*)(bool)> performance_switch{};
std::atomic<std::uint64_t(*)()> performance_counter{};
std::atomic<std::string(*)()> performance_details{};
std::mutex details_mutex;
std::string last_details;
jclass overlay_class{};
jmethodID report_method{};
}
namespace wa2::android {
void set_performance_switch(void(*callback)(bool),std::uint64_t(*counter)(),std::string(*details)())noexcept{
    performance_details.store(details,std::memory_order_release);
    performance_counter.store(counter,std::memory_order_release);performance_switch.store(callback,std::memory_order_release);
    if(!callback){std::lock_guard lock(details_mutex);last_details.clear();}
}
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
    // Read thread-local game/render counters here on their owner thread. The
    // Java receiver only reads this bounded copy, never live game memory.
    try{if(const auto details=performance_details.load(std::memory_order_acquire)){
        auto value=details();if(value.size()<=8192){std::lock_guard lock(details_mutex);last_details=std::move(value);}
    }}catch(...){std::lock_guard lock(details_mutex);last_details.clear();}
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());if(!env)return;
    env->CallStaticVoidMethod(overlay_class,report_method,jfloat(sample->fps),jfloat(sample->average_ms));
    // Closing an optional HUD must not terminate the game thread.
    if(env->ExceptionCheck())env->ExceptionClear();
}
}
extern "C" JNIEXPORT void JNICALL Java_org_wa2_port_Wa2Activity_nativeResetFrameMetrics(JNIEnv*,jclass){
    reset_requested.store(true,std::memory_order_release);
}
extern "C" JNIEXPORT jboolean JNICALL Java_org_wa2_port_PerformanceProbe_nativeSetCandidate(JNIEnv*,jclass,jboolean enabled){
    const auto callback=performance_switch.load(std::memory_order_acquire);
    if(!callback)return JNI_FALSE;callback(enabled==JNI_TRUE);return JNI_TRUE;
}
extern "C" JNIEXPORT jlong JNICALL Java_org_wa2_port_PerformanceProbe_nativeCandidateCalls(JNIEnv*,jclass){
    const auto counter=performance_counter.load(std::memory_order_acquire);return counter?jlong(counter()):jlong(-1);
}
extern "C" JNIEXPORT jstring JNICALL Java_org_wa2_port_PerformanceProbe_nativeTextWork(JNIEnv* env,jclass){
    std::lock_guard lock(details_mutex);return env->NewStringUTF(last_details.c_str());
}
