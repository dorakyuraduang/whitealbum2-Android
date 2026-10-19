#include "resource_mount.hpp"
#include "../file_io.hpp"
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <algorithm>
#include <cerrno>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <sstream>
namespace wa2::platform::compat {
namespace {
std::string key(std::string_view name){
    std::string result(name);for(auto& c:result){if(c=='\\')c='/';else if(c>='A' && c<='Z')c=char(c+32);}
    while(result.starts_with("./"))result.erase(0,2);if(result==".")result.clear();return result;
}
bool archive_name(std::string_view name){
    if(name.starts_with("ic/"))name.remove_prefix(3);if(name.size()<5 || name.size()>128 || !name.ends_with(".pak") || name.front()=='.')return false;
    for(const char c:name)if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.'))return false;return true;
}
std::string logical(wr::Process& p,const std::filesystem::path& path){const auto relative=path.lexically_relative(p.options.data_root);if(relative.empty())return path==p.options.data_root?"":"../";
    if(relative.begin()!=relative.end() && *relative.begin()=="..")return "../";return relative.generic_string();}
std::string missing_archive(wr::Process& p,std::string_view name){
    std::ostringstream out;out<<"resource archive not indexed in the authorized PAK directory: "<<name
        <<"\ngame current directory: C:\\WinRecomp";
    if(!p.state().current_directory.empty())out<<'\\'<<p.state().current_directory;
    for(const auto* directory:{"","IC"}){
        const auto entries=p.state().read_only_files->list(directory);out<<"\n"<<(*directory?directory:"root")<<" indexed entries ("<<entries.size()<<"): ";
        unsigned shown{};for(const auto& item:entries){if(shown++)out<<", ";out<<item.name;if(shown==12){if(entries.size()>12)out<<", ...";break;}}
    }return out.str();
}
class StreamSource final:public resource::Source {
    struct Close {void operator()(std::FILE* file) const noexcept {if(file)std::fclose(file);}};
    std::unique_ptr<std::FILE,Close> file_;std::uint64_t size_{};std::optional<std::uint64_t> cursor_;std::mutex mutex_;
public:
    explicit StreamSource(std::FILE* file,std::uint64_t size):file_(file),size_(size){if(!file_)throw resource::Error("cannot open mounted archive");}
    std::uint64_t size() const override{return size_;}
    void read_at(std::uint64_t offset,std::span<std::uint8_t> bytes) override{
        if(offset>size_ || bytes.size()>size_-offset)throw resource::Error("mounted archive read exceeds file size");if(bytes.empty())return;
        std::lock_guard lock(mutex_);if(!cursor_ || *cursor_!=offset){if(!seek_stream(file_.get(),offset))throw resource::Error("cannot seek mounted archive");}
        cursor_.reset();if(std::fread(bytes.data(),1,bytes.size(),file_.get())!=bytes.size())throw resource::Error("short mounted archive read");cursor_=offset+bytes.size();
    }
};
}
ResourceMount::ResourceMount(std::vector<wr::ReadOnlyFileInfo> files,std::function<std::FILE*(std::string_view)> open):open_(std::move(open)){
    if(!open_)throw std::invalid_argument("resource opener is required");if(files.size()>10000)throw std::invalid_argument("too many mounted archives");
    for(auto& info:files){const auto folded=key(info.name);if(!archive_name(folded))throw std::invalid_argument("invalid mounted archive filename");
        info.attributes=0x21;if(!files_.emplace(folded,std::move(info)).second)throw std::invalid_argument("ambiguous mounted archive filename");}
}
std::optional<wr::ReadOnlyFileInfo> ResourceMount::info(std::string_view path) const{
    const auto folded=key(path);if(folded.empty())return wr::ReadOnlyFileInfo{"",0,0,0x11};
    if(folded=="ic"){const auto first=files_.lower_bound("ic/");if(first!=files_.end() && first->first.starts_with("ic/"))return wr::ReadOnlyFileInfo{"IC",0,0,0x11};return {};}
    const auto found=files_.find(folded);return found==files_.end()?std::nullopt:std::optional(found->second);
}
std::vector<wr::ReadOnlyFileInfo> ResourceMount::list(std::string_view directory) const{
    const auto folded=key(directory);std::vector<wr::ReadOnlyFileInfo> result;if(!folded.empty() && folded!="ic")return result;
    const std::string prefix=folded.empty()?"":"ic/";bool have_ic=false;
    for(const auto& [name,item]:files_){if(folded.empty() && name.starts_with("ic/")){have_ic=true;continue;}
        if(!name.starts_with(prefix))continue;auto copy=item;const auto slash=copy.name.find_last_of("/\\");if(slash!=std::string::npos)copy.name.erase(0,slash+1);result.push_back(std::move(copy));}
    if(have_ic)result.push_back({"IC",0,0,0x11});return result;
}
std::FILE* ResourceMount::open(std::string_view name){const auto found=files_.find(key(name));if(found==files_.end()){errno=ENOENT;return nullptr;}return open_(found->second.name);}
void install_resource_mount(wr::Process& p,std::shared_ptr<wr::ReadOnlyFiles> mount){
    if(!mount)throw std::invalid_argument("missing resource mount");if(p.entry)throw std::logic_error("resource mount must be installed before image load");
    if(p.options.allow_file_write && mount->info("IC"))std::filesystem::create_directory(p.options.data_root/"IC");p.state().read_only_files=std::move(mount);
}
std::optional<wr::ReadOnlyFileInfo> mounted_resource_info(wr::Process& p,const std::filesystem::path& path){
    return p.state().read_only_files?p.state().read_only_files->info(logical(p,path)):std::nullopt;
}
std::shared_ptr<resource::Source> open_process_resource(wr::Process& p,const std::filesystem::path& path){
    const auto info=mounted_resource_info(p,path);const auto name=logical(p,path);
    if(!info){
        // Mounted PAK names belong to the provider. A missing logical archive
        // must not silently fall through to the unrelated private-save folder.
        if(p.state().read_only_files && archive_name(key(name)))throw resource::NotFound(missing_archive(p,name));
        return open_resource_file(path);
    }
    if(info->attributes&16)throw resource::Error("resource archive path names a directory: "+name);
    std::FILE* file{};
    try{file=p.state().read_only_files->open(name);}
    catch(const resource::Error& error){throw resource::Error("cannot open mounted resource archive: "+name+" ("+error.what()+")");}
    if(!file){const auto failure=errno;throw resource::Error("cannot open mounted resource archive: "+name+
        (failure?" ("+std::error_code(failure,std::generic_category()).message()+")":""));}
    try{return std::make_shared<StreamSource>(file,info->size);}catch(...){std::fclose(file);throw;}
}
}
