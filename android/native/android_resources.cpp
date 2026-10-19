#include "android_resources.hpp"
#include "resource_jni.hpp"
#include "resource_fd.hpp"
#include <SDL_system.h>
#include <string>

namespace wa2::android {
namespace {
jclass directory_class{};
jmethodID open_method{};
jmethodID optional_method{};
jmethodID failure_method{};
jmethodID list_method{},metadata_method{};

std::string consume_error(JNIEnv* env) {
    auto error=env->ExceptionOccurred();
    if(!error)return "Android resource directory request failed";
    env->ExceptionClear();
    // Bounded local-reference lifetime even when the provider throws.
    std::string message="Android resource directory request failed";
    auto type=env->GetObjectClass(error);
    auto method=type ? env->GetMethodID(type,"toString","()Ljava/lang/String;") : nullptr;
    auto text=method ? static_cast<jstring>(env->CallObjectMethod(error,method)) : nullptr;
    if(!env->ExceptionCheck() && text) {
        auto chars=env->GetStringUTFChars(text,nullptr);
        if(chars) {
            try {message=chars;}
            catch(...) {env->ReleaseStringUTFChars(text,chars);env->DeleteLocalRef(text);
                if(type)env->DeleteLocalRef(type);env->DeleteLocalRef(error);throw;}
            env->ReleaseStringUTFChars(text,chars);
        }
    }
    if(env->ExceptionCheck())env->ExceptionClear();
    if(text)env->DeleteLocalRef(text);
    if(type)env->DeleteLocalRef(type);
    env->DeleteLocalRef(error);
    return message;
}
}
bool initialize_resource_jni(JNIEnv* env) noexcept {
    auto local=env->FindClass("org/wa2/port/ResourceDirectory");
    if(!local)return false;
    directory_class=static_cast<jclass>(env->NewGlobalRef(local));env->DeleteLocalRef(local);
    if(!directory_class)return false;
    open_method=env->GetStaticMethodID(directory_class,"openPak","(Ljava/lang/String;)I");
    if(open_method)optional_method=env->GetStaticMethodID(directory_class,"openOptionalPak","(Ljava/lang/String;)I");
    if(optional_method)failure_method=env->GetStaticMethodID(directory_class,"reportFailure","(Ljava/lang/String;)V");
    if(failure_method)list_method=env->GetStaticMethodID(directory_class,"listPaks","()[Ljava/lang/String;");
    if(list_method)metadata_method=env->GetStaticMethodID(directory_class,"pakMetadata","(Ljava/lang/String;)[J");
    if(!open_method || !optional_method || !failure_method || !list_method || !metadata_method){release_resource_jni(env);return false;}
    return true;
}
void release_resource_jni(JNIEnv* env) noexcept {
    if(directory_class)env->DeleteGlobalRef(directory_class);
    directory_class=nullptr;open_method=nullptr;optional_method=nullptr;failure_method=nullptr;
    list_method=nullptr;metadata_method=nullptr;
}
int open_resource_descriptor(std::string_view filename,bool optional) {
    if(!directory_class || !open_method)throw resource::Error("Android resource directory bridge is unavailable");
    if(filename.empty() || filename.size()>128)throw resource::Error("invalid resource archive filename");
    for(unsigned char c:filename)if(c==0 || c>=128)throw resource::Error("invalid resource archive filename");
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if(!env)throw resource::Error("Android resource thread has no JNI environment");
    const std::string name(filename);
    auto local=env->NewStringUTF(name.c_str());
    if(!local)throw resource::Error(consume_error(env));
    const auto fd=env->CallStaticIntMethod(directory_class,optional?optional_method:open_method,local);
    env->DeleteLocalRef(local);
    if(env->ExceptionCheck())throw resource::Error(consume_error(env));
    if(fd<0 && !optional)throw resource::Error("Android provider returned no archive descriptor");return fd;
}
std::shared_ptr<resource::Source> open_resource_bank(std::string_view filename){return platform::adopt_resource_fd(open_resource_descriptor(filename));}
std::shared_ptr<resource::Source> try_open_resource_bank(std::string_view filename){const auto fd=open_resource_descriptor(filename,true);return fd<0?nullptr:platform::adopt_resource_fd(fd);}
std::vector<ArchiveMetadata> resource_metadata(){
    if(!directory_class || !list_method || !metadata_method)throw resource::Error("Android resource metadata bridge is unavailable");
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());if(!env)throw resource::Error("Android resource thread has no JNI environment");
    auto names=static_cast<jobjectArray>(env->CallStaticObjectMethod(directory_class,list_method));if(env->ExceptionCheck())throw resource::Error(consume_error(env));
    if(!names)throw resource::Error("Android returned no resource index");
    std::vector<ArchiveMetadata> result;
    try{const auto count=env->GetArrayLength(names);if(count<0 || count>10000)throw resource::Error("invalid Android resource index length");result.reserve(std::size_t(count));
        for(jsize n=0;n<count;++n){if(env->PushLocalFrame(8)<0)throw resource::Error(consume_error(env));
            try{auto name=static_cast<jstring>(env->GetObjectArrayElement(names,n));if(!name || env->ExceptionCheck())throw resource::Error(consume_error(env));
                ArchiveMetadata record;const auto* utf8=env->GetStringUTFChars(name,nullptr);if(!utf8)throw resource::Error(consume_error(env));
                try{record.name=utf8;}catch(...){env->ReleaseStringUTFChars(name,utf8);throw;}env->ReleaseStringUTFChars(name,utf8);
                auto metadata=static_cast<jlongArray>(env->CallStaticObjectMethod(directory_class,metadata_method,name));if(env->ExceptionCheck())throw resource::Error(consume_error(env));
                if(!metadata || env->GetArrayLength(metadata)!=2)throw resource::Error("invalid Android archive metadata");
                jlong values[2]{};env->GetLongArrayRegion(metadata,0,2,values);if(env->ExceptionCheck())throw resource::Error(consume_error(env));
                if(values[0]<0 || values[1]<0)throw resource::Error("negative Android archive metadata");record.size=std::uint64_t(values[0]);record.modified_millis=std::uint64_t(values[1]);result.push_back(std::move(record));
            }catch(...){env->PopLocalFrame(nullptr);throw;}env->PopLocalFrame(nullptr);
        }
    }catch(...){env->DeleteLocalRef(names);throw;}env->DeleteLocalRef(names);return result;
}
void report_game_failure(std::string_view message) noexcept{
    if(!directory_class || !failure_method)return;
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());if(!env)return;
    try{const std::string value(message.substr(0,4096));auto local=env->NewStringUTF(value.c_str());
        if(local){env->CallStaticVoidMethod(directory_class,failure_method,local);env->DeleteLocalRef(local);}
    }catch(...){}
    if(env->ExceptionCheck())env->ExceptionClear();
}
}
