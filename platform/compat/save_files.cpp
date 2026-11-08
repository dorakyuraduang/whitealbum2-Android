#include "save_files.hpp"
#include "../../engine/test_progress.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <map>
#include <stdexcept>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
namespace wa2::platform::compat {namespace {
using U32=std::uint32_t;using Info=wr::ReadOnlyFileInfo;
std::string fold(std::string_view value){std::string result(value);for(auto& c:result){if(c=='\\')c='/';else if(c>='A' && c<='Z')c=char(c+32);}return result;}
constexpr std::string_view main_folder="documents/leaf/white album2";
bool file_name(std::string_view name){return name=="sys.sav" || name=="system.ini" || name=="d3d.ini" || name=="default.ini" || name=="save_q.sav" ||
    (name.size()==11 && name.starts_with("save_") && name.ends_with(".sav") && name[5]>='0' && name[5]<='9' && name[6]>='0' && name[6]<='9');}
bool folder_name(std::string_view name){return name.empty() || name=="documents" || name=="documents/leaf" || name==main_folder || name=="documents/leaf/white album2 -closing chapter-" || name=="documents/leaf/white album2 -introductory chapter-";}
std::optional<std::string> mapped(std::string_view value,bool folder=false){
    auto path=fold(value);if(path==".")path.clear();if(path.find('\0')!=path.npos || path.find(':')!=path.npos || path.starts_with('/') || path.find("..")!=path.npos)return {};
    if(folder){if(!folder_name(path))return {};return path==main_folder?std::string{}:path;}
    const auto slash=path.find_last_of('/');const auto directory=slash==path.npos?std::string{}:path.substr(0,slash),name=slash==path.npos?path:path.substr(slash+1);
    if(!file_name(name) || !folder_name(directory))return {};
    if(!directory.empty() && directory!="documents/leaf/white album2" && !directory.starts_with("documents/leaf/white album2 -"))return {};
    return directory.empty() || directory==main_folder?name:path;
}
class Saves final:public wr::MutableFiles {
    std::shared_ptr<wr::MutableFiles> files_;std::string prefix_;
    std::string physical(std::string_view name){return name.empty() && !prefix_.empty()?prefix_.substr(0,prefix_.size()-1):prefix_+std::string(name);}
    std::pair<std::string,std::optional<Info>> locate(const std::string& name){
        auto path=physical(name);auto info=files_->info(path);if(info)return {std::move(path),std::move(info)};
        if(name.find('/')==name.npos && file_name(name)){const auto legacy=physical(std::string(main_folder)+"/"+name);if(auto found=files_->info(legacy))return {legacy,std::move(found)};}
        return {std::move(path),{}};
    }
    std::string existing(const std::string& name){return locate(name).first;}
public:
    std::string identity(std::string_view value)override{const auto name=mapped(value);return name?existing(*name):physical(fold(value));}
    Saves(std::shared_ptr<wr::MutableFiles> files,bool test):files_(std::move(files)),prefix_(test?"test-mode/":""){
        if(!files_)throw std::invalid_argument("Missing selected game directory");
        if(test){const auto error=files_->directory("test-mode");if(error && error!=183)throw std::runtime_error("Cannot create selected-directory test profile");}
    }
    std::optional<Info> info(std::string_view value)override{
        if(auto directory=mapped(value,true)){if(directory->empty() || *directory=="documents" || *directory=="documents/leaf")return Info{std::string(value),0,0,16};auto result=files_->info(physical(*directory));if(result)result->name=std::string(value);return result;}
        const auto path=mapped(value);if(!path)return {};auto result=locate(*path).second;if(result)result->name=std::string(value);return result;
    }
    std::vector<Info> list(std::string_view value)override{
        const auto path=mapped(value,true);if(!path)return {};std::map<std::string,Info> result;
        const auto add=[&](std::string_view directory){for(auto item:files_->list(physical(directory))){const auto logical=path->empty()?item.name:*path+"/"+item.name;if((item.attributes&16)?folder_name(fold(logical)):mapped(logical).has_value())result.try_emplace(fold(item.name),std::move(item));}};
        add(*path);if(path->empty())add(main_folder);
        if(path->empty())result.try_emplace("documents",Info{"Documents",0,0,16});
        if(*path=="documents")result.try_emplace("leaf",Info{"Leaf",0,0,16});
        if(*path=="documents/leaf")result.try_emplace("white album2",Info{"WHITE ALBUM2",0,0,16});
        std::vector<Info> entries;for(auto& [key,item]:result)entries.push_back(std::move(item));return entries;
    }
    std::FILE* open(std::string_view value,bool read,bool write,U32 creation,U32& error)override{
        const auto path=mapped(value);if(!path){error=5;return nullptr;}return files_->open((creation==3 || creation==4 || creation==5)?existing(*path):physical(*path),read,write,creation,error);
    }
    OpenPlan prepare_open(std::string_view value)override{
        const auto name=mapped(value);
        if(!name)return {{},physical(fold(value)),[](bool,bool,U32,U32& error)->std::FILE*{error=5;return nullptr;}};
        auto [found,info]=locate(*name);
        return {std::move(info),found,[files=files_,existing=found,primary=physical(*name)](bool read,bool write,U32 creation,U32& error){
            return files->open((creation==3 || creation==4 || creation==5)?existing:primary,read,write,creation,error);
        }};
    }
    U32 directory(std::string_view value)override{const auto path=mapped(value,true);if(!path)return 5;if(path->empty())return 183;std::string parent;std::size_t at{};while((at=path->find('/',at))!=path->npos){parent=path->substr(0,at++);const auto error=files_->directory(physical(parent));if(error && error!=183)return error;}return files_->directory(physical(*path));}
    U32 remove(std::string_view value,bool directory)override{const auto path=mapped(value,directory);if(!path || directory)return 5;return files_->remove(existing(*path),false);}
    U32 move(std::string_view from,std::string_view to)override{const auto a=mapped(from),b=mapped(to);if(!a || !b)return 5;if(info(to))return 183;return files_->move(existing(*a),physical(*b));}
    U32 attributes(std::string_view value,U32 flags)override{const auto path=mapped(value);return path?files_->attributes(existing(*path),flags):5;}
    void replace(std::string_view value,std::span<const std::uint8_t> bytes)override{const auto path=mapped(value);if(!path || bytes.size()>64u*1024*1024)throw std::runtime_error("Unsupported selected-directory save/configuration path: "+std::string(value));files_->replace(physical(*path),bytes);}
};
class Local final:public wr::MutableFiles {
    std::filesystem::path root_;
    std::filesystem::path path(std::string_view name){auto result=std::filesystem::weakly_canonical(root_/std::filesystem::path(name));const auto relative=result.lexically_relative(root_);if(relative.empty() || *relative.begin()=="..")throw std::runtime_error("Selected-directory path escapes its root");return result;}
public:
    explicit Local(std::filesystem::path root):root_(std::filesystem::canonical(root)){}
    std::optional<Info> info(std::string_view name)override{const auto file=path(name);std::error_code error;const auto status=std::filesystem::status(file,error);if(error==std::errc::no_such_file_or_directory)return {};if(error)throw std::filesystem::filesystem_error("Cannot inspect selected game file",file,error);if(!std::filesystem::exists(status))return {};
        const bool directory=std::filesystem::is_directory(status);if(!directory && !std::filesystem::is_regular_file(status))throw std::runtime_error("Unsupported selected game file type");const auto stamp=std::chrono::duration_cast<std::chrono::seconds>(std::filesystem::file_time_type::clock::to_sys(std::filesystem::last_write_time(file)).time_since_epoch()).count();return Info{file.filename().string(),directory?0:std::filesystem::file_size(file),stamp>=-11644473600ll?std::uint64_t(stamp+11644473600ll)*10000000ull:0,directory?16u:128u};}
    std::vector<Info> list(std::string_view name)override{const auto file=path(name);if(!info(name))return {};std::vector<Info> result;for(const auto& entry:std::filesystem::directory_iterator(file)){if(result.size()>=10000)throw std::runtime_error("Selected game directory is too large");auto item=info(entry.path().lexically_relative(root_).generic_string());if(item)result.push_back(std::move(*item));}return result;}
    std::FILE* open(std::string_view name,bool read,bool write,U32 creation,U32& error)override{
        const auto file=path(name);const auto prior=info(name);if(prior && (prior->attributes&16)){error=5;return nullptr;}if(creation<1 || creation>5){error=87;return nullptr;}if(creation==1 && prior){error=80;return nullptr;}if((creation==3 || creation==5) && !prior){error=2;return nullptr;}
        int flags=read?(write?O_RDWR:O_RDONLY):O_WRONLY;if(creation==1)flags|=O_CREAT|O_EXCL;else if(creation==2)flags|=O_CREAT|O_TRUNC;else if(creation==4)flags|=O_CREAT;else if(creation==5)flags|=O_TRUNC;
#ifdef _WIN32
        const int fd=_wopen(file.c_str(),flags|_O_BINARY,_S_IREAD|_S_IWRITE);auto* stream=fd<0?nullptr:_fdopen(fd,read?(write?"r+b":"rb"):"wb");if(fd>=0 && !stream)_close(fd);
#else
        const int fd=::open(file.c_str(),flags|O_CLOEXEC,0600);auto* stream=fd<0?nullptr:fdopen(fd,read?(write?"r+b":"rb"):"wb");if(fd>=0 && !stream)::close(fd);
#endif
        if(!stream)error=errno==ENOENT?3:5;return stream;
    }
    U32 directory(std::string_view name)override{std::error_code error;const auto made=std::filesystem::create_directory(path(name),error);return made?0:error?3:183;}
    U32 remove(std::string_view name,bool directory)override{const auto found=info(name);if(!found)return 2;if(bool(found->attributes&16)!=directory)return 5;std::error_code error;return std::filesystem::remove(path(name),error)?0:5;}
    U32 move(std::string_view from,std::string_view to)override{if(info(to))return 183;if(!info(from))return 2;std::error_code error;std::filesystem::rename(path(from),path(to),error);return error?5:0;}
    U32 attributes(std::string_view name,U32 flags)override{if(flags!=128 && flags!=0)return 5;return info(name)?0:2;}
    void replace(std::string_view name,std::span<const std::uint8_t> bytes)override{static std::atomic<U32> serial{};const auto target=path(name);auto staged=target;staged+=".wa2-"+std::to_string(++serial)+".tmp";try{std::ofstream out(staged,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));out.close();if(!out)throw std::runtime_error("Cannot write selected-directory configuration");std::filesystem::rename(staged,target);}catch(...){std::error_code error;std::filesystem::remove(staged,error);throw;}}
};
}
std::shared_ptr<wr::MutableFiles> local_files(const std::filesystem::path& root){return std::make_shared<Local>(root);}
std::shared_ptr<wr::MutableFiles> save_files(std::shared_ptr<wr::MutableFiles> source,bool test){
    auto result=std::make_shared<Saves>(std::move(source),test);
    if(test){for(const auto* folder:{"Documents","Documents/Leaf","Documents/Leaf/WHITE ALBUM2 -closing chapter-","Documents/Leaf/WHITE ALBUM2 -introductory chapter-"}){const auto error=result->directory(folder);if(error && error!=183)throw std::runtime_error("Cannot prepare selected-directory test saves");}
        for(const auto* file:{"Sys.sav","Documents/Leaf/WHITE ALBUM2 -closing chapter-/Sys.sav","Documents/Leaf/WHITE ALBUM2 -introductory chapter-/Sys.sav"}){
            std::vector<std::uint8_t> bytes(testing::system_save_bytes);U32 error{};auto* opened=result->open(file,true,false,3,error);
            if(opened){std::unique_ptr<std::FILE,decltype(&std::fclose)> stream(opened,std::fclose);if(std::fread(bytes.data(),1,bytes.size(),opened)!=bytes.size() || std::fgetc(opened)!=EOF)throw std::runtime_error("Unsupported test Sys.sav length");}
            else if(error!=2 && error!=3)throw std::runtime_error("Cannot read selected-directory test Sys.sav");
            const bool changed=testing::unlock_all_content(bytes);if(!opened || changed)result->replace(file,bytes);
        }
    }
    return result;
}
}
