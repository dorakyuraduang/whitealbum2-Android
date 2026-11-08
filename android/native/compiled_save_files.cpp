#include "compiled_resources.hpp"
#include "resource_jni.hpp"
#include <winrecomp/readonly_files.hpp>
#include <SDL_system.h>
#include <array>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <unistd.h>
namespace wa2::android {namespace {
using U32=std::uint32_t;
struct Frame {
    JNIEnv* env=static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    Frame(){if(!env || !resource_directory_class())throw std::runtime_error("Selected game-directory JNI is unavailable");if(env->PushLocalFrame(16)<0){check();throw std::runtime_error("Cannot allocate game-directory JNI frame");}}
    ~Frame(){env->PopLocalFrame(nullptr);}
    void check(){if(!env->ExceptionCheck())return;auto error=env->ExceptionOccurred();env->ExceptionClear();std::string message="Selected game-directory operation failed";if(error){auto type=env->GetObjectClass(error);auto method=type?env->GetMethodID(type,"toString","()Ljava/lang/String;"):nullptr;if(method){auto text=static_cast<jstring>(env->CallObjectMethod(error,method));if(text && !env->ExceptionCheck()){const auto* data=env->GetStringUTFChars(text,nullptr);if(data){message=data;env->ReleaseStringUTFChars(text,data);}}}if(env->ExceptionCheck())env->ExceptionClear();}throw std::runtime_error(message);}
    jstring name(std::string_view value){if(value.size()>160 || value.find('\0')!=value.npos)throw std::runtime_error("Invalid selected game-directory name");auto result=env->NewStringUTF(std::string(value).c_str());check();if(!result)throw std::runtime_error("Cannot allocate game-file name");return result;}
    std::string text(jstring value){if(!value || env->GetStringUTFLength(value)>256)throw std::runtime_error("Invalid selected game-directory metadata");const auto* data=env->GetStringUTFChars(value,nullptr);check();if(!data)throw std::runtime_error("Cannot read game-directory metadata");std::string result;try{result=data;}catch(...){env->ReleaseStringUTFChars(value,data);throw;}env->ReleaseStringUTFChars(value,data);return result;}
};
class Files final:public wr::MutableFiles {
    jmethodID open_{},metadata_{},operation_{},replace_{};jint token_{};
    std::vector<wr::ReadOnlyFileInfo> metadata(std::string_view path,bool list){
        Frame frame;auto name=frame.name(path);auto result=static_cast<jobjectArray>(frame.env->CallStaticObjectMethod(resource_directory_class(),metadata_,name,token_,jboolean(list)));frame.check();if(!result)throw std::runtime_error("Missing selected-directory metadata");const auto count=frame.env->GetArrayLength(result);if(count<0 || count>40000 || count%4 || (!list && count>4))throw std::runtime_error("Invalid selected-directory metadata length");
        std::vector<wr::ReadOnlyFileInfo> entries;
        for(jsize at=0;at<count;at+=4){std::array<std::string,4> fields;for(unsigned n=0;n<4;++n){auto item=static_cast<jstring>(frame.env->GetObjectArrayElement(result,at+n));frame.check();fields[n]=frame.text(item);frame.env->DeleteLocalRef(item);}const auto number=[](const auto& text){std::uint64_t value{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);if(text.empty() || parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())throw std::runtime_error("Invalid selected-directory numeric metadata");return value;};
            const auto millis=number(fields[2]),attributes=number(fields[3]);if(millis>(std::numeric_limits<std::uint64_t>::max()-116444736000000000ull)/10000 || (attributes!=16 && attributes!=128))throw std::runtime_error("Selected-directory metadata is out of range");
            entries.push_back({std::move(fields[0]),number(fields[1]),millis?millis*10000+116444736000000000ull:0,U32(attributes)});
        }return entries;
    }
    U32 operation(std::string_view path,std::string_view target,int code,U32 flags=0){Frame frame;auto name=frame.name(path),to=frame.name(target);const auto result=frame.env->CallStaticIntMethod(resource_directory_class(),operation_,name,to,token_,jint(code),jint(flags));frame.check();if(result<0)throw std::runtime_error("Invalid selected-directory operation result");return U32(result);}
public:
    Files(){Frame frame;auto type=resource_directory_class();const auto method=[&](const char* name,const char* signature){auto result=frame.env->GetStaticMethodID(type,name,signature);frame.check();if(!result)throw std::runtime_error("Missing selected-directory JNI method");return result;};
        token_=frame.env->CallStaticIntMethod(type,method("gameStorageToken","()I"));frame.check();
        open_=method("compatibleOpen","(Ljava/lang/String;III)I");metadata_=method("compatibleMetadata","(Ljava/lang/String;IZ)[Ljava/lang/String;");operation_=method("compatibleOperation","(Ljava/lang/String;Ljava/lang/String;III)I");replace_=method("compatibleReplace","(Ljava/lang/String;ILjava/nio/ByteBuffer;)V");
    }
    std::optional<wr::ReadOnlyFileInfo> info(std::string_view name)override{auto entries=metadata(name,false);return entries.empty()?std::nullopt:std::optional(std::move(entries.front()));}
    std::vector<wr::ReadOnlyFileInfo> list(std::string_view name)override{return metadata(name,true);}
    std::FILE* open(std::string_view name,bool read,bool write,U32 creation,U32& error)override{Frame frame;auto text=frame.name(name);const auto fd=frame.env->CallStaticIntMethod(resource_directory_class(),open_,text,token_,jint((read?1:0)|(write?2:0)),jint(creation));frame.check();if(fd<0){error=U32(-fd);return nullptr;}auto* stream=fdopen(fd,read?(write?"r+b":"rb"):"wb");if(!stream){close(fd);throw std::runtime_error("Cannot own selected game-file descriptor");}return stream;}
    U32 directory(std::string_view name)override{return operation(name,{},0);}
    U32 remove(std::string_view name,bool directory)override{return operation(name,{},directory?2:1);}
    U32 move(std::string_view from,std::string_view to)override{return operation(from,to,3);}
    U32 attributes(std::string_view name,U32 flags)override{return operation(name,{},4,flags);}
    void replace(std::string_view name,std::span<const std::uint8_t> bytes)override{if(bytes.size()>64u*1024*1024)throw std::runtime_error("Selected game file exceeds write bound");Frame frame;auto text=frame.name(name);static std::uint8_t empty{};auto buffer=frame.env->NewDirectByteBuffer(bytes.empty()?&empty:const_cast<std::uint8_t*>(bytes.data()),jlong(bytes.size()));frame.check();if(!buffer)throw std::runtime_error("Cannot expose game-file publication buffer");frame.env->CallStaticVoidMethod(resource_directory_class(),replace_,text,token_,buffer);frame.check();}
};
}
std::shared_ptr<wr::MutableFiles> compiled_save_files(){return std::make_shared<Files>();}
}
