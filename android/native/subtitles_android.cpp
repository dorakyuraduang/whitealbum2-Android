#include "subtitles_android.hpp"
#include <SDL_system.h>
#include <string>
#include <stdexcept>

namespace wa2::android {
namespace {
jclass subtitles_class{};jmethodID read_method{},raster_method{};
std::string exception(JNIEnv* env){
    auto error=env->ExceptionOccurred();if(!error)return "Android subtitle operation failed";env->ExceptionClear();std::string message="Android subtitle operation failed";
    auto cls=env->GetObjectClass(error);auto method=cls?env->GetMethodID(cls,"toString","()Ljava/lang/String;"):nullptr;auto value=method?static_cast<jstring>(env->CallObjectMethod(error,method)):nullptr;
    if(value && !env->ExceptionCheck()){const auto* chars=env->GetStringUTFChars(value,nullptr);if(chars){try{message=chars;}catch(...){env->ReleaseStringUTFChars(value,chars);if(cls)env->DeleteLocalRef(cls);env->DeleteLocalRef(value);env->DeleteLocalRef(error);throw;}env->ReleaseStringUTFChars(value,chars);}}
    if(env->ExceptionCheck())env->ExceptionClear();if(value)env->DeleteLocalRef(value);if(cls)env->DeleteLocalRef(cls);env->DeleteLocalRef(error);return message;
}
void check(JNIEnv* env){if(env->ExceptionCheck())throw std::runtime_error(exception(env));}
std::vector<std::uint8_t> bytes(JNIEnv* env,jbyteArray array){
    const auto size=env->GetArrayLength(array);check(env);if(size<=0 || std::size_t(size)>subtitles::maximum_yaml_bytes)throw std::runtime_error("sub.yaml must be nonempty and no larger than 1 MiB");
    std::vector<std::uint8_t> result(std::size_t(size),0);env->GetByteArrayRegion(array,0,size,reinterpret_cast<jbyte*>(result.data()));check(env);return result;
}
std::vector<subtitles::GlyphMask> rasterize(JNIEnv* env,std::span<const char32_t> characters){
    if(!subtitles_class || !raster_method)throw std::runtime_error("Subtitle font bridge is unavailable");
    auto array=env->NewIntArray(jsize(characters.size()));check(env);if(!array)throw std::bad_alloc();
    std::vector<jint> points;for(auto cp:characters)points.push_back(jint(cp));env->SetIntArrayRegion(array,0,jsize(points.size()),points.data());check(env);
    auto encoded=static_cast<jintArray>(env->CallStaticObjectMethod(subtitles_class,raster_method,array));env->DeleteLocalRef(array);check(env);if(!encoded)throw std::runtime_error("Subtitle font returned no glyphs");
    const auto count=env->GetArrayLength(encoded);if(count<1 || std::size_t(count)>1+1536u*(4+36*36)){env->DeleteLocalRef(encoded);throw std::runtime_error("Subtitle font exceeded its memory bound");}
    std::vector<jint> raw(std::size_t(count),0);env->GetIntArrayRegion(encoded,0,count,raw.data());env->DeleteLocalRef(encoded);check(env);
    if(raw[0]!=jint(characters.size()))throw std::runtime_error("Incorrect subtitle font glyph count");
    std::size_t at=1;std::vector<subtitles::GlyphMask> result;result.reserve(characters.size());
    for(std::size_t n=0;n<characters.size();++n){if(raw.size()-at<4)throw std::runtime_error("Truncated subtitle font metadata");const auto left=raw[at++],top=raw[at++],width=raw[at++],height=raw[at++];if(left < -40 || left>40 || top < -40 || top>40 || width<0 || width>36 || height<0 || height>36 || std::size_t(width*height)>raw.size()-at)throw std::runtime_error("Invalid subtitle font dimensions");
        subtitles::GlyphMask glyph{left,top,unsigned(width),unsigned(height),{}};glyph.coverage.reserve(std::size_t(width*height));for(int p=0;p<width*height;++p){const auto a=raw[at++];if(a<0 || a>255)throw std::runtime_error("Invalid subtitle font coverage");glyph.coverage.push_back(std::uint8_t(a));}result.push_back(std::move(glyph));}
    if(at!=raw.size())throw std::runtime_error("Trailing subtitle font data");return result;
}
std::shared_ptr<const subtitles::Catalogue> compile(JNIEnv* env,jbyteArray array){return subtitles::compile_external(subtitles::parse_yaml(bytes(env,array)),[env](auto characters){return rasterize(env,characters);});}
}
bool initialize_subtitles_jni(JNIEnv* env)noexcept{
    const auto local=env->FindClass("org/wa2/port/ExternalSubtitles");if(!local)return false;subtitles_class=static_cast<jclass>(env->NewGlobalRef(local));env->DeleteLocalRef(local);if(!subtitles_class)return false;
    read_method=env->GetStaticMethodID(subtitles_class,"readForGame","()[B");if(read_method)raster_method=env->GetStaticMethodID(subtitles_class,"rasterize","([I)[I");
    if(!read_method || !raster_method){release_subtitles_jni(env);return false;}return true;
}
void release_subtitles_jni(JNIEnv* env)noexcept{if(subtitles_class)env->DeleteGlobalRef(subtitles_class);subtitles_class=nullptr;read_method=raster_method=nullptr;}
std::shared_ptr<const subtitles::Catalogue> selected_subtitles(){
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());if(!env || !subtitles_class || !read_method)throw std::runtime_error("External subtitle bridge is unavailable");
    if(env->PushLocalFrame(12)<0)throw std::runtime_error(exception(env));
    try{auto array=static_cast<jbyteArray>(env->CallStaticObjectMethod(subtitles_class,read_method));check(env);auto result=array?compile(env,array):nullptr;env->PopLocalFrame(nullptr);return result;}catch(...){env->PopLocalFrame(nullptr);throw;}
}
}
extern "C" JNIEXPORT jstring JNICALL Java_org_wa2_port_ExternalSubtitles_nativeValidate(JNIEnv* env,jclass,jbyteArray array){
    try{
        if(!array)throw std::runtime_error("No external subtitle document");
        const auto catalogue=wa2::android::compile(env,array);std::size_t cues{};for(const auto& track:catalogue->tracks())cues+=track.cues.size();
        const auto text=std::to_string(catalogue->tracks().size())+" audio tracks / "+std::to_string(cues)+" subtitle cues";return env->NewStringUTF(text.c_str());
    }catch(const std::exception& error){if(!env->ExceptionCheck()){const auto cls=env->FindClass("java/io/IOException");if(cls)env->ThrowNew(cls,error.what());}return nullptr;}
}
