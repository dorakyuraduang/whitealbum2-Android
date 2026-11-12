#include "compiled_resources.hpp"
#include "resource_jni.hpp"
#include "../../platform/compat/resource_mount.hpp"
#include "../../platform/clock.hpp"
#include "../../platform/compat/codepage.hpp"
#include <SDL_system.h>
#include <charconv>
#include <array>
#include <cerrno>
#include <cstdio>
#include <limits>
#include <unistd.h>
namespace wa2::android {
namespace {
struct Frame {
    JNIEnv* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    Frame(){if(!env || !resource_directory_class())throw std::runtime_error("Resource-directory JNI is unavailable");if(env->PushLocalFrame(16)<0)throw std::runtime_error("Cannot allocate loose-resource JNI frame");}
    ~Frame(){env->PopLocalFrame(nullptr);}
    void check(){if(!env->ExceptionCheck())return;auto error=env->ExceptionOccurred();env->ExceptionClear();std::string message="Loose resource access failed";if(error){auto type=env->GetObjectClass(error);auto method=type?env->GetMethodID(type,"toString","()Ljava/lang/String;"):nullptr;if(method){auto text=static_cast<jstring>(env->CallObjectMethod(error,method));if(text && !env->ExceptionCheck()){const auto* value=env->GetStringUTFChars(text,nullptr);if(value){message=value;env->ReleaseStringUTFChars(text,value);}}}if(env->ExceptionCheck())env->ExceptionClear();}throw std::runtime_error(message);}
    jstring name(std::string_view value){if(value.size()>1024 || value.find('\0')!=value.npos)throw std::runtime_error("Invalid loose resource path");const auto wide=platform::compat::decode_text(65001,{reinterpret_cast<const std::uint8_t*>(value.data()),value.size()},true);auto text=env->NewString(reinterpret_cast<const jchar*>(wide.data()),jsize(wide.size()));check();if(!text)throw std::runtime_error("Cannot allocate loose resource path");return text;}
    std::string text(jstring value){if(!value || env->GetStringLength(value)>1024)throw std::runtime_error("Invalid loose resource metadata");const auto* chars=env->GetStringChars(value,nullptr);check();if(!chars)throw std::runtime_error("Cannot read loose resource metadata");std::u16string wide;try{wide.assign(reinterpret_cast<const char16_t*>(chars),env->GetStringLength(value));}catch(...){env->ReleaseStringChars(value,chars);throw;}env->ReleaseStringChars(value,chars);const auto bytes=platform::compat::encode_text(65001,wide,{},true);return {bytes.bytes.begin(),bytes.bytes.end()};}
};
class LooseResources final:public wr::ReadOnlyFiles {
    jint token_{};jmethodID metadata_{},open_{};
    std::vector<wr::ReadOnlyFileInfo> records(std::string_view path,bool list)const{
        Frame frame;auto name=frame.name(path);auto array=static_cast<jobjectArray>(frame.env->CallStaticObjectMethod(resource_directory_class(),metadata_,name,token_,jboolean(list)));frame.check();
        if(!array)throw std::runtime_error("Missing loose resource metadata");const auto count=frame.env->GetArrayLength(array);if(count%4 || count>65536*4 || (!list && count>4))throw std::runtime_error("Invalid loose resource metadata size");
        const auto number=[](const std::string& text){std::uint64_t value{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);if(text.empty() || parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())throw std::runtime_error("Invalid loose resource numeric metadata");return value;};
        std::vector<wr::ReadOnlyFileInfo> result;result.reserve(count/4);
        for(jsize n=0;n<count;n+=4){std::array<std::string,4> fields;for(unsigned k=0;k<4;++k){auto item=static_cast<jstring>(frame.env->GetObjectArrayElement(array,n+k));frame.check();fields[k]=frame.text(item);frame.env->DeleteLocalRef(item);}
            const auto millis=number(fields[2]),attributes=number(fields[3]);if(millis>(std::numeric_limits<std::uint64_t>::max()-116444736000000000ull)/10000 || (attributes!=16 && attributes!=128))throw std::runtime_error("Loose resource metadata exceeds range");
            result.push_back({std::move(fields[0]),number(fields[1]),millis?millis*10000+116444736000000000ull:0,std::uint32_t(attributes)});
        }return result;
    }
public:
    LooseResources(){Frame frame;auto type=resource_directory_class();const auto method=[&](const char* name,const char* signature){auto value=frame.env->GetStaticMethodID(type,name,signature);frame.check();if(!value)throw std::runtime_error("Missing loose resource JNI method");return value;};
        token_=frame.env->CallStaticIntMethod(type,method("gameStorageToken","()I"));frame.check();metadata_=method("looseResourceMetadata","(Ljava/lang/String;IZ)[Ljava/lang/String;");open_=method("openLooseResource","(Ljava/lang/String;I)I");
    }
    std::optional<wr::ReadOnlyFileInfo> info(std::string_view name)const override{auto result=records(name,false);return result.empty()?std::nullopt:std::optional(std::move(result.front()));}
    std::vector<wr::ReadOnlyFileInfo> list(std::string_view name)const override{return records(name,true);}
    std::FILE* open(std::string_view path)override{Frame frame;auto name=frame.name(path);const auto fd=frame.env->CallStaticIntMethod(resource_directory_class(),open_,name,token_);frame.check();if(fd<0){errno=ENOENT;return nullptr;}auto* stream=fdopen(fd,"rb");if(!stream){const auto saved=errno;close(fd);errno=saved;}return stream;}
};
}
void mount_compiled_resources(wr::Process& process){
    std::vector<wr::ReadOnlyFileInfo> files;
    for(auto& record:resource_metadata()){
        if(record.modified_millis>(std::numeric_limits<std::uint64_t>::max()-116444736000000000ull)/10000)
            throw std::runtime_error("Android archive timestamp exceeds source FILETIME range");
        files.push_back({std::move(record.name),record.size,record.modified_millis?record.modified_millis*10000+116444736000000000ull:0});
    }
    auto mount=std::make_shared<platform::compat::ResourceMount>(std::move(files),[](std::string_view name)->std::FILE*{
        const auto descriptor=open_resource_descriptor(name);auto* file=fdopen(descriptor,"rb");if(!file){const auto saved=errno;close(descriptor);errno=saved;}return file;
    },std::make_shared<LooseResources>());
    platform::compat::install_resource_mount(process,std::move(mount));
}
}
