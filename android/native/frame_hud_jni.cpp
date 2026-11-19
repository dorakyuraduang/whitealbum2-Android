#include "android_host.hpp"
#include "frame_metrics.hpp"
#include "frame_hud_jni.hpp"
#include <SDL_system.h>
#include <atomic>
#include <chrono>
#include <time.h>

namespace {
wa2::android::FrameMetrics metrics;
std::atomic<bool> reset_requested{true};
std::atomic<void(*)(wa2::android::TextProbeMode)> performance_switch{};
std::atomic<std::uint64_t(*)(wa2::android::TextProbeCounter)> performance_counter{};
jclass overlay_class{};
jmethodID report_method{};
}
namespace wa2::android {
void set_performance_switch(void(*callback)(TextProbeMode),std::uint64_t(*counter)(TextProbeCounter))noexcept{
    performance_counter.store(counter,std::memory_order_release);
    performance_switch.store(callback,std::memory_order_release);
}
bool initialize_frame_hud_jni(JNIEnv* env)noexcept{
    if(overlay_class || report_method)return false;
    const auto local=env->FindClass("org/wa2/port/FrameHud");if(!local)return false;
    overlay_class=static_cast<jclass>(env->NewGlobalRef(local));env->DeleteLocalRef(local);if(!overlay_class)return false;
    report_method=env->GetStaticMethodID(overlay_class,"report","(FFJJJJJJJJJ)V");if(!report_method)return false;
    reset_requested.store(true,std::memory_order_release);return true;
}
void release_frame_hud_jni(JNIEnv* env)noexcept{
    if(overlay_class)env->DeleteGlobalRef(overlay_class);overlay_class=nullptr;report_method=nullptr;
    reset_requested.store(true,std::memory_order_release);
}
void frame_presented()noexcept{
    frame_presented_with_text(nullptr);
}
void frame_presented_with_text(GlyphTotals (*read_totals)()) noexcept {
    if(reset_requested.exchange(false,std::memory_order_acq_rel))metrics.reset();
    const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const auto sample=metrics.presented(now);if(!sample || !overlay_class || !report_method)return;
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());if(!env)return;
    // Sample existing game-thread counters at the HUD cadence. A snapshot
    // labels a frame window by its workload without logging player text.
    const auto totals = read_totals ? read_totals() : GlyphTotals{};
    timespec cpu{};
    const jlong thread_cpu_ns=clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpu)==0
        ? jlong(cpu.tv_sec)*1000000000LL+jlong(cpu.tv_nsec) : -1;
    env->CallStaticVoidMethod(overlay_class,report_method,jfloat(sample->fps),jfloat(sample->average_ms),
        jlong(sample->presented),jlong(totals.glyphs),jlong(totals.tiles),jlong(now),thread_cpu_ns,
        jlong(totals.replayed_glyphs),jlong(totals.reused_geometry),
        jlong(totals.rollovers),jlong(totals.avoided_driver_calls));
    // Closing an optional HUD must not terminate the game thread.
    if(env->ExceptionCheck())env->ExceptionClear();
}
}
extern "C" JNIEXPORT void JNICALL Java_org_wa2_port_Wa2Activity_nativeResetFrameMetrics(JNIEnv*,jclass){
    reset_requested.store(true,std::memory_order_release);
}
extern "C" JNIEXPORT jboolean JNICALL Java_org_wa2_port_PerformanceProbe_nativeSetCandidate(JNIEnv*,jclass,jint mode){
    using Mode=wa2::android::TextProbeMode;
    const auto callback=performance_switch.load(std::memory_order_acquire);
    if(!callback || mode<0 || mode>int(Mode::novel))return JNI_FALSE;
    callback(static_cast<Mode>(mode));
    return JNI_TRUE;
}
extern "C" JNIEXPORT jlong JNICALL Java_org_wa2_port_PerformanceProbe_nativeCounter(JNIEnv*,jclass,jint index){
    using Counter=wa2::android::TextProbeCounter;
    const auto counter=performance_counter.load(std::memory_order_acquire);
    return counter && index>=0 && index<int(Counter::count)?jlong(counter(static_cast<Counter>(index))):jlong(-1);
}
