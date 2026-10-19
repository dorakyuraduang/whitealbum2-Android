#include "widgets_jni.hpp"
#include "../../platform/window/native_widgets.hpp"
#include "../../platform/compat/codepage.hpp"
#include <SDL_system.h>
#include <winrecomp/image.hpp>
#include <jni.h>
#include <deque>
#include <mutex>
#include <sstream>
extern "C" JNIEXPORT void JNICALL Java_org_wa2_port_NativeWidgets_nativeEvent(JNIEnv*,jclass,jint,jint,jint,jint,jint,jint,jint,jstring);
namespace {
jclass widgets_class{};jmethodID create_method{},update_method{},destroy_method{},show_method{},title_method{},focus_method{},menu_method{};
std::mutex event_mutex;std::deque<wa2::window::Event> pending;std::string failure;
void check(JNIEnv* env){if(env->ExceptionCheck()){env->ExceptionClear();throw std::runtime_error("Android native widget request failed");}}
JNIEnv* environment(){auto* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());if(!env || !widgets_class)throw std::runtime_error("Android widget bridge unavailable");return env;}
std::string json_text(std::u16string_view value){const auto utf8=wa2::platform::compat::encode_text(65001,value).bytes;return wr::quote(std::string(utf8.begin(),utf8.end()));}
std::string dialog_json(const wa2::window::DialogState& d){std::ostringstream out;out<<"{\"token\":"<<d.token<<",\"owner\":"<<d.owner<<",\"title\":"<<json_text(d.definition.title)<<",\"visible\":"<<(d.visible?"true":"false")<<",\"width\":"<<d.definition.bounds[2]<<",\"height\":"<<d.definition.bounds[3]<<",\"controls\":[";
    bool first=true;for(const auto& c:d.controls){if(!first)out<<',';first=false;std::u16string type;if(const auto* ordinal=std::get_if<std::uint16_t>(&c.definition.type)){const auto digits=std::to_string(*ordinal);type=u"#";type.append(digits.begin(),digits.end());}else if(const auto* name=std::get_if<std::u16string>(&c.definition.type))type=*name;
        out<<"{\"id\":"<<c.definition.id<<",\"token\":"<<c.token<<",\"type\":"<<json_text(type)<<",\"text\":"<<json_text(c.text)<<",\"style\":"<<c.definition.style<<",\"bounds\":[";
        for(unsigned n=0;n<4;++n){if(n)out<<',';out<<c.definition.bounds[n];}out<<"],\"check\":"<<c.check<<",\"position\":"<<c.position<<",\"minimum\":"<<c.minimum<<",\"maximum\":"<<c.maximum<<",\"selection\":"<<c.selection<<",\"enabled\":"<<(c.enabled?"true":"false")<<",\"visible\":"<<(c.visible?"true":"false")<<",\"items\":[";
        bool first_item=true;for(const auto& item:c.items){if(!first_item)out<<',';first_item=false;out<<json_text(item);}out<<"]}";}out<<"]}";return out.str();}
std::string menu_json(const std::vector<wa2::window::MenuItem>& items){std::ostringstream out;out<<'[';bool first=true;for(const auto& item:items){if(!first)out<<',';first=false;out<<"{\"id\":"<<item.id<<",\"type\":"<<item.type<<",\"state\":"<<item.state<<",\"text\":"<<json_text(item.text)<<",\"children\":"<<menu_json(item.children)<<'}';}out<<']';return out.str();}
void json_call(jmethodID method,std::string text){auto* env=environment();if(text.size()>1024*1024)throw std::runtime_error("native widget JSON limit");const auto wide=wa2::platform::compat::decode_text(65001,std::span(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()),true);
    const auto string=env->NewString(reinterpret_cast<const jchar*>(wide.data()),jsize(wide.size()));check(env);if(!string)throw std::bad_alloc();env->CallStaticVoidMethod(widgets_class,method,string);env->DeleteLocalRef(string);check(env);}
class AndroidWidgets final:public wa2::window::Widgets {
public:
    void parent(SDL_Window*,std::uint32_t)override{}
    void create(const wa2::window::DialogState& value)override{json_call(create_method,dialog_json(value));}
    void update(const wa2::window::DialogState& value)override{json_call(update_method,dialog_json(value));}
    void destroy(std::uint32_t id)override{auto* env=environment();env->CallStaticVoidMethod(widgets_class,destroy_method,jint(id));check(env);}
    void show(std::uint32_t id,bool visible)override{auto* env=environment();env->CallStaticVoidMethod(widgets_class,show_method,jint(id),jboolean(visible));check(env);}
    void title(std::uint32_t id,std::u16string_view title)override{auto* env=environment();const auto value=env->NewString(reinterpret_cast<const jchar*>(title.data()),jsize(title.size()));check(env);if(!value)throw std::bad_alloc();env->CallStaticVoidMethod(widgets_class,title_method,jint(id),value);env->DeleteLocalRef(value);check(env);}
    void focus(std::uint32_t id)override{auto* env=environment();env->CallStaticVoidMethod(widgets_class,focus_method,jint(id));check(env);}
    void menu(std::uint32_t id,const wa2::window::MenuTemplate& model)override{auto* env=environment();const auto text=menu_json(model.items);const auto wide=wa2::platform::compat::decode_text(65001,std::span(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()),true);const auto value=env->NewString(reinterpret_cast<const jchar*>(wide.data()),jsize(wide.size()));check(env);if(!value)throw std::bad_alloc();env->CallStaticVoidMethod(widgets_class,menu_method,jint(id),value);env->DeleteLocalRef(value);check(env);}
    std::vector<wa2::window::Event> events()override{std::lock_guard lock(event_mutex);if(!failure.empty())throw std::runtime_error(failure);std::vector<wa2::window::Event> result;result.reserve(pending.size());while(!pending.empty()){result.push_back(std::move(pending.front()));pending.pop_front();}return result;}
};
}
namespace wa2::android {
bool initialize_widgets_jni(JNIEnv* env)noexcept{const auto local=env->FindClass("org/wa2/port/NativeWidgets");if(!local)return false;widgets_class=static_cast<jclass>(env->NewGlobalRef(local));env->DeleteLocalRef(local);if(!widgets_class)return false;
    create_method=env->GetStaticMethodID(widgets_class,"create","(Ljava/lang/String;)V");if(create_method)update_method=env->GetStaticMethodID(widgets_class,"update","(Ljava/lang/String;)V");if(update_method)destroy_method=env->GetStaticMethodID(widgets_class,"destroy","(I)V");if(destroy_method)show_method=env->GetStaticMethodID(widgets_class,"show","(IZ)V");if(show_method)title_method=env->GetStaticMethodID(widgets_class,"title","(ILjava/lang/String;)V");if(title_method)focus_method=env->GetStaticMethodID(widgets_class,"focus","(I)V");if(focus_method)menu_method=env->GetStaticMethodID(widgets_class,"menu","(ILjava/lang/String;)V");
    JNINativeMethod event_method{const_cast<char*>("nativeEvent"),const_cast<char*>("(IIIIIIILjava/lang/String;)V"),reinterpret_cast<void*>(Java_org_wa2_port_NativeWidgets_nativeEvent)};
    if(!create_method || !update_method || !destroy_method || !show_method || !title_method || !focus_method || !menu_method || env->RegisterNatives(widgets_class,&event_method,1)!=0){release_widgets_jni(env);return false;}return true;}
void release_widgets_jni(JNIEnv* env)noexcept{if(widgets_class)env->DeleteGlobalRef(widgets_class);widgets_class=nullptr;create_method=update_method=destroy_method=show_method=title_method=focus_method=menu_method=nullptr;std::lock_guard lock(event_mutex);pending.clear();failure.clear();}
}
extern "C" JNIEXPORT void JNICALL Java_org_wa2_port_NativeWidgets_nativeEvent(JNIEnv* env,jclass,jint kind,jint window,jint message,jint wp,jint lp,jint control,jint value,jstring text){
    try{std::lock_guard lock(event_mutex);if(kind<0){failure="Android native widgets failed";if(text){const auto* chars=env->GetStringChars(text,nullptr);if(chars){const auto count=env->GetStringLength(text);const auto bytes=wa2::platform::compat::encode_text(65001,std::u16string_view(reinterpret_cast<const char16_t*>(chars),std::size_t(count))).bytes;failure.assign(bytes.begin(),bytes.end());env->ReleaseStringChars(text,chars);}}return;}
        if(kind>4 || pending.size()>=8192){failure="invalid Android widget event or event queue limit";return;}
        wa2::window::Event event{static_cast<wa2::window::Event::Kind>(kind),std::uint32_t(window),std::uint32_t(message),std::uint32_t(wp),std::uint32_t(lp),std::uint32_t(control),value};
        if(text){const auto count=env->GetStringLength(text);if(count>1048576){failure="Android widget text limit";return;}const auto* chars=env->GetStringChars(text,nullptr);if(!chars){failure="Android widget text allocation failed";return;}try{event.text.assign(reinterpret_cast<const char16_t*>(chars),std::size_t(count));}catch(...){env->ReleaseStringChars(text,chars);throw;}env->ReleaseStringChars(text,chars);}pending.push_back(std::move(event));
    }catch(...){std::lock_guard lock(event_mutex);failure="Android widget event allocation failed";}
}
namespace wa2::window {std::shared_ptr<Widgets> native_widgets(){return std::make_shared<AndroidWidgets>();}}

