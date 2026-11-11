#include "save_files.hpp"
#include "../../engine/test_progress.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>
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
constexpr std::string_view main_folder_name="Documents/Leaf/WHITE ALBUM2";
bool file_name(std::string_view name){return name=="sys.sav" || name=="system.ini" || name=="d3d.ini" || name=="default.ini" || name=="save_q.sav" ||
    (name.size()==11 && name.starts_with("save_") && name.ends_with(".sav") && name[5]>='0' && name[5]<='9' && name[6]>='0' && name[6]<='9');}
bool folder_name(std::string_view name){return name.empty() || name=="documents" || name=="documents/leaf" || name==main_folder || name=="documents/leaf/white album2 -closing chapter-" || name=="documents/leaf/white album2 -introductory chapter-";}
bool save_name(std::string_view name){return file_name(name) && name.ends_with(".sav");}
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
    SaveReadScope scope_;
    struct Inventory {
        std::optional<std::uint64_t> key;std::chrono::steady_clock::time_point expires;
        std::optional<std::uint64_t> declined_key;std::chrono::steady_clock::time_point declined_until;
        std::map<std::string,std::map<std::string,Info>> directories;
        bool main_prepared{};
        std::uint64_t revision{},builds{},hits{},chapter_builds{},chapter_hits{},declines{},info_calls{},list_calls{},open_calls{},prepared_reads{},info_ns{},list_ns{},open_ns{};
        void invalidate(){key.reset();declined_key.reset();}
        struct Mutation {
            Inventory& value;
            explicit Mutation(Inventory& v):value(v){value.invalidate();++value.revision;}
            ~Mutation(){value.invalidate();++value.revision;}
        };
        struct Timer {
            std::uint64_t& calls;std::uint64_t& total;std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
            Timer(std::uint64_t& c,std::uint64_t& t):calls(c),total(t){++calls;}
            ~Timer(){total+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();}
        };
    };
    std::shared_ptr<Inventory> inventory_=std::make_shared<Inventory>();
    bool main_save(std::string_view name)const{return prefix_.empty() && name.find('/')==name.npos && save_name(name);}
    std::string physical(std::string_view name){
        if(main_save(name))return std::string(main_folder_name)+"/"+(name=="sys.sav"?"Sys.sav":name=="save_q.sav"?"save_Q.sav":std::string(name));
        return name.empty() && !prefix_.empty()?prefix_.substr(0,prefix_.size()-1):prefix_+std::string(name);
    }
    static U32 prepare_main(const std::shared_ptr<wr::MutableFiles>& files,Inventory& cache,bool required){
        if(!required || cache.main_prepared)return 0;
        for(const auto* folder:{"Documents","Documents/Leaf","Documents/Leaf/WHITE ALBUM2"}){
            const auto error=files->directory(folder);if(error && error!=183)return error;
        }
        cache.main_prepared=true;return 0;
    }
    std::optional<Info> inspect(std::string_view name){Inventory::Timer timer(inventory_->info_calls,inventory_->info_ns);return files_->info(name);}
    std::vector<Info> entries(std::string_view name){Inventory::Timer timer(inventory_->list_calls,inventory_->list_ns);return files_->list(name);}
    bool snapshot(){
        const auto key=scope_?scope_():std::nullopt;auto& cache=*inventory_;
        if(!key){cache.invalidate();return false;}
        const auto now=std::chrono::steady_clock::now();
        if(cache.declined_key==key && now<cache.declined_until)return false;
        if(cache.key==key && now<cache.expires){++cache.hits;return true;}
        cache.invalidate();cache.directories.clear();cache.key=key;
        cache.expires=now+std::chrono::milliseconds(250);return true;
    }
    std::pair<std::string,std::optional<Info>> locate_exact(std::string path){
        const auto slash=path.find_last_of('/');
        const auto directory=slash==path.npos?std::string{}:path.substr(0,slash);
        const auto leaf=fold(slash==path.npos?path:path.substr(slash+1));
        if(file_name(leaf) && snapshot()){
            auto& cache=*inventory_;const auto key=fold(directory);
            auto found=cache.directories.find(key);
            if(found==cache.directories.end()){
                std::map<std::string,Info> index;
                try{for(auto item:entries(directory)){
                    const auto name=fold(item.name);
                    if(file_name(name) && !index.emplace(name,std::move(item)).second)throw std::runtime_error("Ambiguous save inventory filename");
                }}catch(const std::runtime_error&){
                    const auto scope=cache.key;cache.invalidate();cache.declined_key=scope;
                    cache.declined_until=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);++cache.declines;
                    return {path,inspect(path)};
                }
                found=cache.directories.emplace(key,std::move(index)).first;
                const auto primary=prefix_.empty()?std::string(main_folder):fold(prefix_.substr(0,prefix_.size()-1));
                if(key==primary)++cache.builds;else ++cache.chapter_builds;
                // Start the bounded lifetime after provider I/O, not before it.
                cache.expires=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
            }else ++cache.chapter_hits;
            if(auto item=found->second.find(leaf);item!=found->second.end())return {std::move(path),item->second};
            return {std::move(path),{}};
        }
        auto info=inspect(path);return {std::move(path),std::move(info)};
    }
    std::pair<std::string,std::optional<Info>> locate(const std::string& name){
        auto found=locate_exact(physical(name));
        // Saves never fall back to a different bank. Existing INI placement is
        // a separate configuration policy and does not admit any .sav file.
        if(!found.second && name.find('/')==name.npos && file_name(name) && !save_name(name)){
            auto nested=locate_exact(physical(std::string(main_folder)+"/"+name));
            if(nested.second)return nested;
        }
        return found;
    }
    std::string existing(const std::string& name){return locate(name).first;}
public:
    std::string identity(std::string_view value)override{const auto name=mapped(value);return name?existing(*name):physical(fold(value));}
    Saves(std::shared_ptr<wr::MutableFiles> files,bool test,SaveReadScope scope):files_(std::move(files)),prefix_(test?"test-mode/":""),scope_(std::move(scope)){
        if(!files_)throw std::invalid_argument("Missing selected game directory");
        if(test){const auto error=files_->directory("test-mode");if(error && error!=183)throw std::runtime_error("Cannot create selected-directory test profile");}
    }
    std::optional<Info> info(std::string_view value)override{
        if(auto directory=mapped(value,true)){if(directory->empty() || *directory=="documents" || *directory=="documents/leaf")return Info{std::string(value),0,0,16};auto result=inspect(physical(*directory));if(result)result->name=std::string(value);return result;}
        const auto path=mapped(value);if(!path)return {};auto result=locate(*path).second;if(result)result->name=std::string(value);return result;
    }
    std::vector<Info> list(std::string_view value)override{
        const auto path=mapped(value,true);if(!path)return {};std::map<std::string,Info> result;
        const auto add=[&](std::string_view directory,bool bank=false){for(auto item:entries(physical(directory))){
            const auto name=fold(item.name);const auto logical=path->empty()?item.name:*path+"/"+item.name;
            if(bank && !save_name(name))continue;
            if(!bank && path->empty() && prefix_.empty() && save_name(name))continue;
            if((item.attributes&16)?folder_name(fold(logical)):mapped(logical).has_value())result.try_emplace(name,std::move(item));
        }};
        add(*path);if(path->empty() && prefix_.empty())add(main_folder_name,true);
        if(path->empty())result.try_emplace("documents",Info{"Documents",0,0,16});
        if(*path=="documents")result.try_emplace("leaf",Info{"Leaf",0,0,16});
        if(*path=="documents/leaf")result.try_emplace("white album2",Info{"WHITE ALBUM2",0,0,16});
        std::vector<Info> entries;for(auto& [key,item]:result)entries.push_back(std::move(item));return entries;
    }
    std::FILE* open(std::string_view value,bool read,bool write,U32 creation,U32& error)override{
        std::optional<Inventory::Mutation> mutation;if(write || creation!=3)mutation.emplace(*inventory_);
        const auto path=mapped(value);if(!path){error=5;return nullptr;}
        auto [resolved,info]=(creation==3 || creation==4 || creation==5)?locate(*path):std::pair<std::string,std::optional<Info>>{physical(*path),{}};
        Inventory::Timer timer(inventory_->open_calls,inventory_->open_ns);
        if(read && !write && creation==3 && info && info->open_read){++inventory_->prepared_reads;return info->open_read(error);}
        if(write || creation!=3){error=prepare_main(files_,*inventory_,main_save(*path));if(error)return nullptr;}
        return files_->open(resolved,read,write,creation,error);
    }
    OpenPlan prepare_open(std::string_view value)override{
        const auto name=mapped(value);
        if(!name)return {{},physical(fold(value)),[](bool,bool,U32,U32& error)->std::FILE*{error=5;return nullptr;}};
        auto [found,info]=locate(*name);
        auto resolved_read=info?info->open_read:decltype(Info::open_read){};
        return {std::move(info),found,[files=files_,cache=inventory_,existing=found,primary=physical(*name),canonical=main_save(*name),resolved_read=std::move(resolved_read)](bool read,bool write,U32 creation,U32& error)->std::FILE*{
            std::optional<Inventory::Mutation> mutation;if(write || creation!=3)mutation.emplace(*cache);
            Inventory::Timer timer(cache->open_calls,cache->open_ns);
            if(read && !write && creation==3 && resolved_read){++cache->prepared_reads;return resolved_read(error);}
            if(write || creation!=3){error=prepare_main(files,*cache,canonical);if(error)return nullptr;}
            return files->open((creation==3 || creation==4 || creation==5)?existing:primary,read,write,creation,error);
        }};
    }
    U32 directory(std::string_view value)override{Inventory::Mutation mutation(*inventory_);const auto path=mapped(value,true);if(!path)return 5;if(path->empty())return 183;std::string parent;std::size_t at{};while((at=path->find('/',at))!=path->npos){parent=path->substr(0,at++);const auto error=files_->directory(physical(parent));if(error && error!=183)return error;}return files_->directory(physical(*path));}
    U32 remove(std::string_view value,bool directory)override{Inventory::Mutation mutation(*inventory_);const auto path=mapped(value,directory);if(!path || directory)return 5;return files_->remove(existing(*path),false);}
    U32 move(std::string_view from,std::string_view to)override{Inventory::Mutation mutation(*inventory_);const auto a=mapped(from),b=mapped(to);if(!a || !b)return 5;if(info(to))return 183;const auto error=prepare_main(files_,*inventory_,main_save(*b));return error?error:files_->move(existing(*a),physical(*b));}
    U32 attributes(std::string_view value,U32 flags)override{Inventory::Mutation mutation(*inventory_);const auto path=mapped(value);return path?files_->attributes(existing(*path),flags):5;}
    void replace(std::string_view value,std::span<const std::uint8_t> bytes)override{Inventory::Mutation mutation(*inventory_);const auto path=mapped(value);if(!path || bytes.size()>64u*1024*1024)throw std::runtime_error("Unsupported selected-directory save/configuration path: "+std::string(value));const auto error=prepare_main(files_,*inventory_,main_save(*path));if(error)throw std::runtime_error("Cannot prepare selected-directory save folder (error "+std::to_string(error)+")");files_->replace(physical(*path),bytes);}
    std::uint64_t revision()const{return inventory_->revision;}
    std::uint64_t activity()const{const auto& c=*inventory_;return c.revision+c.info_calls+c.list_calls+c.open_calls+c.hits+c.chapter_hits;}
    std::string report()const{const auto& c=*inventory_;std::ostringstream out;
        out<<"{\"save_policy\":\"canonical-only\",\"root_save_fallback\":false,\"normal_save_directory\":\"Documents/Leaf/WHITE ALBUM2\",\"inventory_builds\":"<<c.builds<<",\"inventory_hits\":"<<c.hits<<",\"chapter_inventory_builds\":"<<c.chapter_builds<<",\"chapter_inventory_hits\":"<<c.chapter_hits<<",\"inventory_declines\":"<<c.declines<<",\"metadata_queries\":"<<c.info_calls<<",\"metadata_ns\":"<<c.info_ns<<",\"directory_queries\":"<<c.list_calls<<",\"directory_ns\":"<<c.list_ns<<",\"descriptor_opens\":"<<c.open_calls<<",\"prepared_read_opens\":"<<c.prepared_reads<<",\"descriptor_open_ns\":"<<c.open_ns<<",\"payload_cached\":false,\"maximum_inventory_age_ms\":250}";return out.str();}
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
std::string save_files_report(const std::shared_ptr<wr::MutableFiles>& files){if(auto* saves=dynamic_cast<const Saves*>(files.get()))return saves->report();return "{}";}
std::optional<std::uint64_t> save_files_revision(const wr::MutableFiles* files){if(auto* saves=dynamic_cast<const Saves*>(files))return saves->revision();return {};}
std::optional<std::uint64_t> save_files_activity(const wr::MutableFiles* files){if(auto* saves=dynamic_cast<const Saves*>(files))return saves->activity();return {};}
std::shared_ptr<wr::MutableFiles> save_files(std::shared_ptr<wr::MutableFiles> source,bool test,SaveReadScope scope){
    auto result=std::make_shared<Saves>(std::move(source),test,std::move(scope));
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
