#include "android_resources.hpp"
#include "resource_jni.hpp"
#include "resource_fd.hpp"
#include "legacy_settings_path.hpp"
#include "legacy_save_path.hpp"
#include <charconv>
#include <array>
#include <SDL_system.h>
#include <string>

namespace wa2::android {
namespace {
jclass directory_class{};
jmethodID open_method{};
jmethodID optional_method{};
jmethodID settings_method{};
jmethodID failure_method{};
jmethodID list_method{},metadata_method{};
jmethodID save_open_method{},save_list_method{};
jmethodID game_token_method{},game_open_method{},game_replace_method{},game_update_method{},game_list_method{};

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
    if(optional_method)settings_method=env->GetStaticMethodID(directory_class,"openLegacySettings","(Ljava/lang/String;)I");
    if(settings_method)failure_method=env->GetStaticMethodID(directory_class,"reportFailure","(Ljava/lang/String;)V");
    if(failure_method)list_method=env->GetStaticMethodID(directory_class,"listPaks","()[Ljava/lang/String;");
    if(list_method)metadata_method=env->GetStaticMethodID(directory_class,"pakMetadata","(Ljava/lang/String;)[J");
    if(metadata_method)save_open_method=env->GetStaticMethodID(directory_class,"openLegacySave","(Ljava/lang/String;)I");
    if(save_open_method)save_list_method=env->GetStaticMethodID(directory_class,"legacySaveMetadata","()[Ljava/lang/String;");
    if(save_list_method)game_token_method=env->GetStaticMethodID(directory_class,"gameStorageToken","()I");
    if(game_token_method)game_open_method=env->GetStaticMethodID(directory_class,"openGameFile","(Ljava/lang/String;I)I");
    if(game_open_method)game_replace_method=env->GetStaticMethodID(directory_class,"replaceGameFile","(Ljava/lang/String;ILjava/nio/ByteBuffer;)V");
    if(game_replace_method)game_update_method=env->GetStaticMethodID(directory_class,"updateGameFile","(Ljava/lang/String;IJJLjava/nio/ByteBuffer;)V");
    if(game_update_method)game_list_method=env->GetStaticMethodID(directory_class,"gameSaveMetadata","(ILjava/lang/String;)[Ljava/lang/String;");
    if(!open_method || !optional_method || !settings_method || !failure_method || !list_method || !metadata_method || !save_open_method || !save_list_method || !game_token_method || !game_open_method || !game_replace_method || !game_update_method || !game_list_method){release_resource_jni(env);return false;}
    return true;
}
void release_resource_jni(JNIEnv* env) noexcept {
    if(directory_class)env->DeleteGlobalRef(directory_class);
    directory_class=nullptr;open_method=nullptr;optional_method=nullptr;settings_method=nullptr;failure_method=nullptr;
    list_method=nullptr;metadata_method=nullptr;
    save_open_method=nullptr;save_list_method=nullptr;
    game_token_method=nullptr;game_open_method=nullptr;game_replace_method=nullptr;game_update_method=nullptr;game_list_method=nullptr;
}
static int open_descriptor(std::string_view filename,jmethodID method,bool optional) {
    if(!directory_class || !open_method)throw resource::Error("Android resource directory bridge is unavailable");
    if(filename.empty() || filename.size()>128)throw resource::Error("invalid resource archive filename");
    for(unsigned char c:filename)if(c==0 || c>=128)throw resource::Error("invalid resource archive filename");
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if(!env)throw resource::Error("Android resource thread has no JNI environment");
    const std::string name(filename);
    auto local=env->NewStringUTF(name.c_str());
    if(!local)throw resource::Error(consume_error(env));
    if(!method){env->DeleteLocalRef(local);throw resource::Error("Android resource method is unavailable");}
    const auto fd=env->CallStaticIntMethod(directory_class,method,local);
    env->DeleteLocalRef(local);
    if(env->ExceptionCheck())throw resource::Error(consume_error(env));
    if(fd<0 && !optional)throw resource::Error("Android provider returned no archive descriptor");return fd;
}
int open_resource_descriptor(std::string_view filename,bool optional){return open_descriptor(filename,optional?optional_method:open_method,optional);}
std::shared_ptr<resource::Source> open_resource_bank(std::string_view filename){return platform::adopt_resource_fd(open_resource_descriptor(filename));}
std::shared_ptr<resource::Source> try_open_resource_bank(std::string_view filename){const auto fd=open_resource_descriptor(filename,true);return fd<0?nullptr:platform::adopt_resource_fd(fd);}
std::shared_ptr<resource::Source> try_open_legacy_settings(std::string_view filename){
    if(!platform::legacy_settings_path(filename))throw resource::Error("Invalid original settings filename");
    const auto fd=open_descriptor(filename,settings_method,true);return fd<0?nullptr:platform::adopt_resource_fd(fd);
}
std::shared_ptr<resource::Source> try_open_legacy_save(std::string_view filename){
    if(!resource::legacy_save_path(filename))throw resource::Error("Invalid original manual-save filename");const auto fd=open_descriptor(filename,save_open_method,true);return fd<0?nullptr:platform::adopt_resource_fd(fd);
}
#if defined(WA2_NATIVE_GAME_STORAGE)
namespace {
class GameDirectory final:public platform::GameDirectory {
    jint token_;
    struct Frame {
        JNIEnv* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
        Frame(){if(!directory_class || !game_list_method || !env)throw resource::Error("Android game-directory bridge is unavailable");if(env->PushLocalFrame(12)<0)throw resource::Error(consume_error(env));}
        ~Frame(){env->PopLocalFrame(nullptr);}
        jstring name(std::string_view name,bool write=false){if(!platform::game_file_path(name,write))throw resource::Error("Invalid Android game-file path");const std::string text(name);auto result=env->NewStringUTF(text.c_str());check();if(!result)throw resource::Error("Cannot allocate game-file name");return result;}
        jobject buffer(std::span<const std::uint8_t> bytes){if(bytes.size()>64u*1024*1024)throw resource::Error("Android game file exceeds write bound");static std::uint8_t empty{};auto result=env->NewDirectByteBuffer(bytes.empty()?&empty:const_cast<std::uint8_t*>(bytes.data()),jlong(bytes.size()));check();if(!result)throw resource::Error("Cannot expose game-file write buffer");return result;}
        void check(){if(env->ExceptionCheck())throw resource::Error(consume_error(env));}
    };
public:
    GameDirectory(){Frame frame;token_=frame.env->CallStaticIntMethod(directory_class,game_token_method);frame.check();}
    std::shared_ptr<resource::Source> open(std::string_view name)override{Frame frame;auto text=frame.name(name);const auto fd=frame.env->CallStaticIntMethod(directory_class,game_open_method,text,token_);frame.check();return fd<0?nullptr:platform::adopt_resource_fd(fd);}
    void replace(std::string_view name,std::span<const std::uint8_t> bytes)override{Frame frame;auto text=frame.name(name,true);auto buffer=frame.buffer(bytes);frame.env->CallStaticVoidMethod(directory_class,game_replace_method,text,token_,buffer);frame.check();}
    void update(std::string_view name,std::uint64_t expected,std::uint64_t offset,std::span<const std::uint8_t> bytes)override{if(expected>64u*1024*1024 || offset>expected || bytes.size()>expected-offset)throw resource::Error("Invalid Android game-file update range");Frame frame;auto text=frame.name(name,true);auto buffer=frame.buffer(bytes);frame.env->CallStaticVoidMethod(directory_class,game_update_method,text,token_,jlong(expected),jlong(offset),buffer);frame.check();}
    std::vector<platform::GameFileInfo> saves(std::string_view directory)override{
        if(directory!="native-saves" && directory!="native-test/native-saves")throw resource::Error("Invalid Android save inventory directory");
        Frame frame;auto name=frame.env->NewStringUTF(std::string(directory).c_str());frame.check();if(!name)throw resource::Error("Cannot allocate native save directory name");auto array=static_cast<jobjectArray>(frame.env->CallStaticObjectMethod(directory_class,game_list_method,token_,name));frame.check();if(!array)throw resource::Error("Android returned no native save inventory");const auto count=frame.env->GetArrayLength(array);if(count<0 || count>3000 || count%3)throw resource::Error("Invalid Android native save inventory");std::vector<platform::GameFileInfo> result;
        for(jsize at=0;at<count;at+=3){std::array<std::string,3> fields;for(unsigned n=0;n<3;++n){auto item=static_cast<jstring>(frame.env->GetObjectArrayElement(array,at+n));frame.check();if(!item || frame.env->GetStringUTFLength(item)>64)throw resource::Error("Invalid Android game-save metadata field");const auto* value=frame.env->GetStringUTFChars(item,nullptr);frame.check();if(!value)throw resource::Error("Cannot read Android game-save metadata");try{fields[n]=value;}catch(...){frame.env->ReleaseStringUTFChars(item,value);throw;}frame.env->ReleaseStringUTFChars(item,value);frame.env->DeleteLocalRef(item);}
            if(!platform::game_slot_file(fields[0]))throw resource::Error("Unknown Android native save filename");platform::GameFileInfo info;info.name=std::move(fields[0]);const auto number=[](const auto& text){std::uint64_t value{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);if(text.empty() || parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())throw resource::Error("Invalid Android native save metadata number");return value;};info.bytes=number(fields[1]);info.modified_ms=number(fields[2]);result.push_back(std::move(info));}return result;
    }
};
}
std::shared_ptr<platform::GameDirectory> selected_game_directory(){return std::make_shared<GameDirectory>();}
#endif
std::vector<ArchiveMetadata> legacy_save_metadata(){
    if(!directory_class || !save_list_method)throw resource::Error("Android original-save metadata bridge is unavailable");auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());if(!env)throw resource::Error("Android save thread has no JNI environment");
    auto array=static_cast<jobjectArray>(env->CallStaticObjectMethod(directory_class,save_list_method));if(env->ExceptionCheck())throw resource::Error(consume_error(env));if(!array)throw resource::Error("Android returned no original-save inventory");std::vector<ArchiveMetadata> result;
    try{const auto count=env->GetArrayLength(array);if(count<0 || count>404*3 || count%3)throw resource::Error("Invalid Android original-save inventory length");result.reserve(std::size_t(count/3));
        const auto string=[&](jsize at){auto item=static_cast<jstring>(env->GetObjectArrayElement(array,at));if(!item || env->ExceptionCheck())throw resource::Error(consume_error(env));if(env->GetStringUTFLength(item)>128)throw resource::Error("Android original-save metadata field exceeds its bound");const auto* text=env->GetStringUTFChars(item,nullptr);if(!text)throw resource::Error(consume_error(env));std::string value;try{value=text;}catch(...){env->ReleaseStringUTFChars(item,text);throw;}env->ReleaseStringUTFChars(item,text);return value;};
        const auto number=[](const std::string& text){std::uint64_t value{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || text.empty())throw resource::Error("Invalid Android original-save size or timestamp");return value;};
        for(jsize at=0;at<count;at+=3){if(env->PushLocalFrame(4)<0)throw resource::Error(consume_error(env));try{ArchiveMetadata entry;entry.name=string(at);if(!resource::legacy_save_path(entry.name))throw resource::Error("Unknown Android original-save path");entry.size=number(string(at+1));entry.modified_millis=number(string(at+2));result.push_back(std::move(entry));}catch(...){env->PopLocalFrame(nullptr);throw;}env->PopLocalFrame(nullptr);}
    }catch(...){env->DeleteLocalRef(array);throw;}env->DeleteLocalRef(array);return result;
}
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
