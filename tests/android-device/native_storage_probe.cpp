#include "process.hpp"
#include "save_read_scope.hpp"
#include "../../platform/compat/save_files.hpp"
#include "../../platform/compat/system.hpp"
#include <winrecomp/win32_state.hpp>
#include <jni.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace {
using wr::U32;
class Provider final:public wr::MutableFiles {
    JNIEnv* env_;jobject documents_;jmethodID metadata_,open_;
    void checked(){if(env_->ExceptionCheck()){env_->ExceptionDescribe();env_->ExceptionClear();throw std::runtime_error("Selected-directory Java call failed");}}
    jstring text(std::string_view value){auto result=env_->NewStringUTF(std::string(value).c_str());checked();return result;}
    std::string text(jstring value){const auto* bytes=env_->GetStringUTFChars(value,nullptr);checked();std::string result(bytes);env_->ReleaseStringUTFChars(value,bytes);return result;}
    std::vector<wr::ReadOnlyFileInfo> query(std::string_view path,bool list){
        auto name=text(path);auto array=static_cast<jobjectArray>(env_->CallObjectMethod(documents_,metadata_,name,jboolean(list)));env_->DeleteLocalRef(name);checked();
        if(!array)throw std::runtime_error("No provider metadata result");
        const auto count=env_->GetArrayLength(array);if(count%4)throw std::runtime_error("Malformed provider metadata result");std::vector<wr::ReadOnlyFileInfo> result;
        for(jsize n=0;n<count;n+=4){std::string fields[4];for(jsize k=0;k<4;++k){auto value=static_cast<jstring>(env_->GetObjectArrayElement(array,n+k));checked();fields[k]=text(value);env_->DeleteLocalRef(value);}
            const auto millis=std::stoull(fields[2]);result.push_back({std::move(fields[0]),std::stoull(fields[1]),millis?millis*10000+116444736000000000ull:0,U32(std::stoul(fields[3]))});}
        env_->DeleteLocalRef(array);return result;
    }
public:
    unsigned metadata_calls{},list_calls{},open_calls{},successful_opens{};
    Provider(JNIEnv* env,jobject documents):env_(env),documents_(documents){auto type=env->GetObjectClass(documents);metadata_=env->GetMethodID(type,"compatibleMetadata","(Ljava/lang/String;Z)[Ljava/lang/String;");open_=env->GetMethodID(type,"compatibleOpen","(Ljava/lang/String;II)I");env->DeleteLocalRef(type);checked();}
    std::optional<wr::ReadOnlyFileInfo> info(std::string_view path)override{++metadata_calls;auto result=query(path,false);return result.empty()?std::nullopt:std::optional(result.front());}
    std::vector<wr::ReadOnlyFileInfo> list(std::string_view path)override{++list_calls;return query(path,true);}
    std::FILE* open(std::string_view path,bool read,bool write,U32 creation,U32& error)override{
        if(!read || write || creation!=3)throw std::runtime_error("Device probe forbids external writes");++open_calls;
        auto name=text(path);const auto fd=env_->CallIntMethod(documents_,open_,name,1,3);env_->DeleteLocalRef(name);checked();if(fd<0){error=U32(-fd);return nullptr;}
        auto* file=fdopen(fd,"rb");if(!file){close(fd);error=5;}else{error=0;++successful_opens;}return file;
    }
    U32 directory(std::string_view)override{throw std::runtime_error("Device probe forbids external mutations");}
    U32 remove(std::string_view,bool)override{throw std::runtime_error("Device probe forbids external mutations");}
    U32 move(std::string_view,std::string_view)override{throw std::runtime_error("Device probe forbids external mutations");}
    U32 attributes(std::string_view,U32)override{throw std::runtime_error("Device probe forbids external mutations");}
    void replace(std::string_view,std::span<const std::uint8_t>)override{throw std::runtime_error("Device probe forbids external mutations");}
};
}
extern "C" JNIEXPORT jstring JNICALL Java_org_wa2_probe_StorageProbe_nativeMeasure(JNIEnv* env,jclass,jstring directory,jobject documents,jstring profile_name,jboolean optimized,jboolean profile,jint count){
    try{
        const auto* chars=env->GetStringUTFChars(directory,nullptr);if(!chars)return nullptr;const std::filesystem::path root(chars);env->ReleaseStringUTFChars(directory,chars);
        const auto* ini_chars=env->GetStringUTFChars(profile_name,nullptr);if(!ini_chars)return nullptr;const std::string ini_name(ini_chars);env->ReleaseStringUTFChars(profile_name,ini_chars);
        wr::ProcessOptions options;options.data_root=root;options.allow_file_write=false;
        wa2::aot::Runtime runtime(options);auto& p=runtime.process();auto& m=p.memory;
        if(!optimized)wa2::platform::compat::install_system(p);
        auto provider=std::make_shared<Provider>(env,documents);
        p.state().mutable_files=wa2::platform::compat::save_files(provider,false,optimized?wa2::platform::compat::SaveReadScope([&]{return wa2::aot::save_read_scope(p);}):wa2::platform::compat::SaveReadScope{});
        runtime.load();if(p.gui() || p.directdraw())throw std::runtime_error("Unexpected device creation in storage probe");
        m.store(0xa3a130,25,32);m.store(0xa39208,7,32);
        const std::string prefix="C:\\WinRecomp";m.copy_in(0xa39d28,{reinterpret_cast<const std::uint8_t*>(prefix.c_str()),prefix.size()+1});
        std::uint64_t checksum{};unsigned found{};
        const auto section=p.put_string("DEFAULT"),key=p.put_string("__wa2_read_only_probe_missing_key__"),filename=p.put_string(ini_name);
        const auto start=std::chrono::steady_clock::now();
        if(count<1 || count>100)throw std::runtime_error("Invalid native probe count");
        for(U32 n=0;n<U32(count);++n){
            const auto value=profile?p.callback(0x44d4e0,std::array<U32,4>{key,n,section,filename},false):p.callback(0x4234c0,std::array<U32,6>{n,0,0,0,0,0},false);
            checksum=checksum*131+value;if(!profile && value)++found;
        }
        const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        if(profile && !provider->successful_opens)throw std::runtime_error("Native profile probe did not open the selected existing INI");
        std::ostringstream result;result<<"{\"optimized\":"<<(optimized?"true":"false")<<",\"profile\":"<<(profile?"true":"false")<<",\"static_calls\":"<<count<<",\"elapsed_ns\":"<<elapsed<<",\"metadata_calls\":"<<provider->metadata_calls<<",\"list_calls\":"<<provider->list_calls<<",\"open_calls\":"<<provider->open_calls<<",\"successful_opens\":"<<provider->successful_opens<<",\"present_slots\":"<<found<<",\"result_checksum\":\""<<checksum<<"\",\"file_io\":"<<p.state().reports.at("file_io")()<<",\"profile_io\":"<<p.state().reports.at("profile_io")()<<",\"selected_directory_io\":"<<wa2::platform::compat::save_files_report(p.state().mutable_files)<<",\"game_launched\":false,\"screen_access\":false,\"passed\":true}";
        return env->NewStringUTF(result.str().c_str());
    }catch(const std::exception& error){auto type=env->FindClass("java/lang/IllegalStateException");if(type)env->ThrowNew(type,error.what());return nullptr;}
}
