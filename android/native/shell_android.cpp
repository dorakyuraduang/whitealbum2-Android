#include "shell_jni.hpp"
#include "../../platform/compat/shell.hpp"
#include "../../platform/compat/codepage.hpp"
#include "../../platform/window/widget_json.hpp"
#include <SDL_system.h>
#include <SDL.h>
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <exception>
#include <limits>
namespace {
jclass widgets_class{};jmethodID pick_method{};
std::mutex mutex;std::condition_variable changed;std::optional<std::filesystem::path> selected;
int next_request{},active_request{};bool returned{},unloaded{};
std::exception_ptr selection_error;
void JNICALL selection(JNIEnv* env,jclass,jint request,jstring path){std::lock_guard lock(mutex);if(request!=active_request || returned)return;
    try{if(path){const auto length=env->GetStringLength(path);if(length>32768)throw std::runtime_error("selected path limit");const auto* text=env->GetStringChars(path,nullptr);if(!text)throw std::bad_alloc();std::u16string wide;try{wide.assign(reinterpret_cast<const char16_t*>(text),std::size_t(length));}catch(...){env->ReleaseStringChars(path,text);throw;}env->ReleaseStringChars(path,text);selected=std::filesystem::path(wide);}else selected.reset();
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();selected.reset();selection_error=std::current_exception();}returned=true;changed.notify_all();}
using wa2::window::json::text;
std::optional<std::filesystem::path> pick(const wa2::platform::compat::FileRequest& request,const std::filesystem::path& root){
    auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());if(!env || !widgets_class || !pick_method)throw std::runtime_error("Android file chooser unavailable");
    int id{};{std::lock_guard lock(mutex);if(active_request || unloaded)throw std::runtime_error("Android file chooser busy or detached");if(next_request==std::numeric_limits<int>::max())next_request=0;active_request=id=++next_request;returned=false;selected.reset();selection_error=nullptr;}
    try{std::ostringstream out;out<<"{\"root\":"<<text(root.u16string())<<",\"initial\":"<<text(request.initial.u16string())<<",\"title\":"<<text(request.title)<<",\"extension\":"<<text(request.extension)<<",\"name\":"<<text(request.suggested_filename)<<",\"save\":"<<(request.save?"true":"false")<<",\"directory\":"<<(request.directory?"true":"false")<<'}';const auto text=out.str();
        const auto utf16=wa2::platform::compat::decode_text(65001,std::span(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()),true);const auto value=env->NewString(reinterpret_cast<const jchar*>(utf16.data()),jsize(utf16.size()));if(!value || env->ExceptionCheck())throw std::runtime_error("Android file chooser string allocation failed");env->CallStaticVoidMethod(widgets_class,pick_method,jint(id),value);env->DeleteLocalRef(value);if(env->ExceptionCheck()){env->ExceptionClear();throw std::runtime_error("Android file chooser request failed");}
        std::unique_lock lock(mutex);changed.wait(lock,[]{return returned || unloaded;});auto result=std::move(selected);const auto failure=selection_error;active_request=0;selected.reset();selection_error=nullptr;lock.unlock();if(failure)std::rethrow_exception(failure);return result;
    }catch(...){std::lock_guard lock(mutex);active_request=0;throw;}
}
}
namespace wa2::android {
bool initialize_shell_jni(JNIEnv* env)noexcept{const auto local=env->FindClass("org/wa2/port/FileChooser");if(!local)return false;widgets_class=static_cast<jclass>(env->NewGlobalRef(local));env->DeleteLocalRef(local);if(!widgets_class)return false;pick_method=env->GetStaticMethodID(widgets_class,"choose","(ILjava/lang/String;)V");
    JNINativeMethod method{const_cast<char*>("nativeSelection"),const_cast<char*>("(ILjava/lang/String;)V"),reinterpret_cast<void*>(selection)};if(!pick_method || env->RegisterNatives(widgets_class,&method,1)!=0){release_shell_jni(env);return false;}{std::lock_guard lock(mutex);unloaded=false;}return true;}
void release_shell_jni(JNIEnv* env)noexcept{if(widgets_class)env->DeleteGlobalRef(widgets_class);widgets_class=nullptr;pick_method=nullptr;{std::lock_guard lock(mutex);unloaded=true;selected.reset();changed.notify_all();}}
}
namespace wa2::platform::compat {
ShellServices native_shell_services(const std::filesystem::path& root){
    return {[root](const FileRequest& request){return pick(request,root);},
        [](std::u16string_view target,std::u16string_view verb){
            if(verb!=u"open" && verb!=u"explore")return false;
            const auto encoded=encode_text(65001,target).bytes;const std::string url(encoded.begin(),encoded.end());
            if(!url.starts_with("https://") && !url.starts_with("http://") && !url.starts_with("mailto:"))return false;
            return SDL_OpenURL(url.c_str())==0;
        }};
}
}
