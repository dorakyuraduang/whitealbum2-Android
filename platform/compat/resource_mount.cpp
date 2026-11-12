#include "resource_mount.hpp"
#include "../file_io.hpp"
#include "codepage.hpp"
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <winrecomp/path_name.hpp>
#include <algorithm>
#include <cerrno>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <sstream>
#include <chrono>
namespace wa2::platform::compat {
namespace {
std::string key(std::string_view name){
    std::string result=wr::normalize_windows_name(name,true);
    while(result.starts_with("./"))result.erase(0,2);if(result==".")result.clear();return result;
}
bool archive_name(std::string_view name){
    if(name.starts_with("ic/"))name.remove_prefix(3);if(name.size()<5 || name.size()>128 || !name.ends_with(".pak") || name.front()=='.')return false;
    for(const char c:name)if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.'))return false;return true;
}
std::string logical(wr::Process& p,const std::filesystem::path& path){const auto relative=path.lexically_relative(p.options.data_root);if(relative.empty())return path==p.options.data_root?"":"../";
    if(relative.begin()!=relative.end() && *relative.begin()=="..")return "../";return wr::utf8_file_name(relative);}
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
// Read-only filesystem adapter. Cache names, never payloads or descriptors.
class LocalLoose final:public wr::ReadOnlyFiles {
    using Path=std::filesystem::path;
    struct Names {std::map<std::string,Path> files;std::set<std::string> ambiguous;std::chrono::steady_clock::time_point expires;std::uint64_t used{};};
    Path root_;mutable std::map<Path,Names> roots_,folders_;mutable std::uint64_t serial_{};
    static std::string utf8(const Path& p){const auto text=p.generic_u8string();return {text.begin(),text.end()};}
    Path inside(const Path& p)const{auto result=std::filesystem::weakly_canonical(p);const auto relative=result.lexically_relative(root_);if(relative.empty() || *relative.begin()=="..")throw resource::Error("Loose resource escapes selected directory");return result;}
    const Names& index(const Path& parent)const{
        const bool permanent=parent==root_ || (parent.parent_path()==root_ && key(utf8(parent.filename()))=="ic");auto& cache=permanent?roots_:folders_;
        const auto now=std::chrono::steady_clock::now();if(auto found=cache.find(parent);found!=cache.end()){
            if(permanent || now<found->second.expires){found->second.used=++serial_;return found->second;}cache.erase(found);
        }
        Names names;names.expires=now+std::chrono::milliseconds(250);names.used=++serial_;std::error_code error;
        if(std::filesystem::is_directory(parent,error))for(const auto& entry:std::filesystem::directory_iterator(parent)){
            if(names.files.size()>=65536)throw resource::Error("Loose resource directory exceeds 65536 entries");const auto name=key(utf8(entry.path().filename()));
            if(!names.files.emplace(name,entry.path()).second)names.ambiguous.insert(name);
        }else if(error && error!=std::errc::no_such_file_or_directory)throw std::filesystem::filesystem_error("Cannot inspect loose resource directory",parent,error);
        if(!permanent){std::size_t count=names.files.size();for(const auto& [p,item]:cache)count+=item.files.size();
            while(!cache.empty() && (cache.size()>=8 || count>65536)){auto oldest=std::min_element(cache.begin(),cache.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});count-=oldest->second.files.size();cache.erase(oldest);}}
        return cache.emplace(parent,std::move(names)).first->second;
    }
    std::optional<Path> resolve(std::string_view name)const{
        if(name.empty())return root_;if(name.size()>1024 || name.front()=='/' || name.find(':')!=name.npos || name.find('\\')!=name.npos)return {};
        Path parent=root_;std::size_t at{};
        while(at<name.size()){const auto end=name.find('/',at);const auto part=name.substr(at,end==name.npos?name.size()-at:end-at);if(part.empty() || part=="." || part=="..")return {};
            const auto& names=index(parent);const auto folded=key(part);if(names.ambiguous.contains(folded))throw resource::Error("Ambiguous loose resource filename");const auto found=names.files.find(folded);if(found==names.files.end())return {};parent=inside(found->second);if(end==name.npos)return parent;at=end+1;
        }return {};
    }
    static std::optional<wr::ReadOnlyFileInfo> describe(const Path& path){std::error_code error;const auto status=std::filesystem::status(path,error);if(error==std::errc::no_such_file_or_directory)return {};if(error)throw std::filesystem::filesystem_error("Cannot inspect loose resource",path,error);if(!std::filesystem::exists(status))return {};
        const bool directory=std::filesystem::is_directory(status);if(!directory && !std::filesystem::is_regular_file(status))throw resource::Error("Unsupported loose resource file type");const auto stamp=std::chrono::duration_cast<std::chrono::nanoseconds>(std::filesystem::file_time_type::clock::to_sys(std::filesystem::last_write_time(path)).time_since_epoch()).count()/100;
        return wr::ReadOnlyFileInfo{utf8(path.filename()),directory?0:std::filesystem::file_size(path),stamp>=-116444736000000000ll?std::uint64_t(stamp+116444736000000000ll):0,directory?0x11u:0x21u};}
public:
    explicit LocalLoose(const Path& root):root_(std::filesystem::canonical(root)){}
    std::optional<wr::ReadOnlyFileInfo> info(std::string_view name)const override{const auto path=resolve(name);return path?describe(*path):std::nullopt;}
    std::vector<wr::ReadOnlyFileInfo> list(std::string_view name)const override{std::vector<wr::ReadOnlyFileInfo> result;const auto parent=resolve(name);if(!parent || !std::filesystem::is_directory(*parent))return result;const auto& names=index(*parent);if(!names.ambiguous.empty())throw resource::Error("Ambiguous loose resource directory");for(const auto& [key,path]:names.files)if(auto item=describe(inside(path)))result.push_back(std::move(*item));return result;}
    std::FILE* open(std::string_view name)override{const auto path=resolve(name);if(!path){errno=ENOENT;return nullptr;}if(std::filesystem::is_directory(*path)){errno=EISDIR;return nullptr;}
#ifdef _WIN32
        return _wfopen(path->c_str(),L"rb");
#else
        return std::fopen(path->c_str(),"rb");
#endif
    }
};
}
ResourceMount::ResourceMount(std::vector<wr::ReadOnlyFileInfo> files,std::function<std::FILE*(std::string_view)> open,std::shared_ptr<wr::ReadOnlyFiles> loose):open_(std::move(open)),loose_(std::move(loose)){
    if(!open_)throw std::invalid_argument("resource opener is required");if(files.size()>10000)throw std::invalid_argument("too many mounted archives");
    for(auto& info:files){const auto folded=key(info.name);if(!archive_name(folded))throw std::invalid_argument("invalid mounted archive filename");
        auto group=std::string_view(folded);if(group.starts_with("ic/"))group.remove_prefix(3);groups_.emplace(group.substr(0,group.size()-4));
        info.attributes=0x21;if(!files_.emplace(folded,std::move(info)).second)throw std::invalid_argument("ambiguous mounted archive filename");}
}
unsigned ResourceMount::loose_kind(std::string_view path)const{
    if(!loose_ || path.empty() || path.size()>1024 || path.front()=='/')return 0;
    std::vector<std::string_view> parts;std::size_t at{};
    while(at<path.size()){const auto end=path.find('/',at);const auto part=path.substr(at,end==path.npos?path.size()-at:end-at);
        if(part.empty() || part=="." || part==".." || part.size()>255 || part.back()=='.' || part.back()==' ')return 0;
        const bool utf8=wr::utf8_path_name(part);for(std::size_t n=0;n<part.size();++n){if(!utf8 && wr::dbcs_path_pair(part,n)){++n;continue;}const auto c=static_cast<unsigned char>(part[n]);if(c<32 || c==127 || std::string_view("\\:<>\"|?*").find(char(c))!=std::string_view::npos)return 0;}parts.push_back(part);if(end==path.npos)break;at=end+1;if(at==path.size())return 0;}
    const unsigned group=parts[0]=="ic"?1:0;if(parts.size()<=group || !groups_.contains(parts[group]))return 0;
    if(parts.size()==group+1)return 1;
    if(parts.size()==group+2)return parts.back()=="buf"?1:2;
    return parts.size()<=8?2:0;
}
bool ResourceMount::owns_path(std::string_view path)const{const auto folded=key(path);return archive_name(folded) || loose_kind(folded);}
std::optional<wr::ReadOnlyFileInfo> ResourceMount::info(std::string_view path) const{
    const auto folded=key(path);if(folded.empty())return wr::ReadOnlyFileInfo{"",0,0,0x11};
    if(folded=="ic"){const auto first=files_.lower_bound("ic/");if(first!=files_.end() && first->first.starts_with("ic/"))return wr::ReadOnlyFileInfo{"IC",0,0,0x11};return {};}
    const auto found=files_.find(folded);if(found!=files_.end())return found->second;
    if(!loose_kind(folded))return {};
    // Paths in the source ANSI ABI can contain GBK or Shift-JIS member names.
    // The device/filesystem adapter receives UTF-8, and performs no transcoding
    // of the resource contents. Prefer an existing UTF-8 spelling.
    std::set<std::string> attempted;
    const auto lookup=[&](std::string name)->std::optional<wr::ReadOnlyFileInfo>{name=wr::normalize_windows_name(name);if(!loose_kind(key(name)) || !attempted.insert(name).second)return {};
        if(auto item=loose_->info(name)){item->name=std::string(path);item->attributes=(item->attributes&16)?0x11:0x21;
            if(!(item->attributes&16))item->open_read=[files=loose_,name](std::uint32_t& error){auto* stream=files->open(name);if(!stream)error=errno==ENOENT?2u:5u;return stream;};return item;}return {};};
    for(const auto cp:{65001u,936u,932u})try{
        const auto wide=decode_text(cp,{reinterpret_cast<const std::uint8_t*>(folded.data()),folded.size()},true);
        const auto encoded=encode_text(65001,wide,{},true);const std::string name(encoded.bytes.begin(),encoded.bytes.end());
        if(auto item=lookup(name))return item;
        // The original Chinese ANSI ABI also carries Shift-JIS atlas member
        // names. Recover those after the host path's GBK-to-UTF-8 conversion.
        if(cp==65001u)try{const auto bytes=encode_text(936,wide,{},true);const auto japanese=decode_text(932,bytes.bytes,true);const auto alternate=encode_text(65001,japanese,{},true);if(auto item=lookup(std::string(alternate.bytes.begin(),alternate.bytes.end())))return item;}catch(const EncodingError&){}
    }catch(const EncodingError&){}
    return {};
}
std::vector<wr::ReadOnlyFileInfo> ResourceMount::list(std::string_view directory) const{
    const auto folded=key(directory);std::vector<wr::ReadOnlyFileInfo> result;
    if(!folded.empty() && folded!="ic"){
        if(!loose_kind(folded))return result;
        for(auto item:loose_->list(folded)){const auto path=folded+"/"+key(item.name);if(!loose_kind(path))continue;item.attributes=(item.attributes&16)?0x11:0x21;result.push_back(std::move(item));}return result;
    }
    const std::string prefix=folded.empty()?"":"ic/";bool have_ic=false;
    for(const auto& [name,item]:files_){if(folded.empty() && name.starts_with("ic/")){have_ic=true;continue;}
        if(!name.starts_with(prefix))continue;auto copy=item;const auto slash=copy.name.find_last_of("/\\");if(slash!=std::string::npos)copy.name.erase(0,slash+1);result.push_back(std::move(copy));}
    if(have_ic)result.push_back({"IC",0,0,0x11});return result;
}
std::FILE* ResourceMount::open(std::string_view name){const auto found=files_.find(key(name));if(found!=files_.end())return open_(found->second.name);
    const auto item=info(name);if(!item){errno=ENOENT;return nullptr;}if(item->attributes&16){errno=EISDIR;return nullptr;}std::uint32_t error{};return item->open_read?item->open_read(error):nullptr;}
void install_resource_mount(wr::Process& p,std::shared_ptr<wr::ReadOnlyFiles> mount){
    if(!mount)throw std::invalid_argument("missing resource mount");if(p.entry)throw std::logic_error("resource mount must be installed before image load");
    if(p.options.allow_file_write && !p.state().mutable_files && mount->info("IC"))std::filesystem::create_directory(p.options.data_root/"IC");p.state().read_only_files=std::move(mount);
}
std::shared_ptr<wr::ReadOnlyFiles> local_resource_mount(const std::filesystem::path& root){
    std::vector<wr::ReadOnlyFileInfo> files;
    for(const auto* sub:{"","IC"}){
        const auto folder=*sub?root/sub:root;
        if(!std::filesystem::exists(folder))continue;
        for(const auto& item:std::filesystem::directory_iterator(folder)){
            if(!item.is_regular_file())continue;
            const auto name=(*sub?std::string(sub)+"/":"")+item.path().filename().string();
            if(archive_name(key(name)))files.push_back({name,item.file_size(),0,0x21});
        }
    }
    return std::make_shared<ResourceMount>(std::move(files),[root](std::string_view name){
#ifdef _WIN32
        return _wfopen((root/std::filesystem::path(name)).c_str(),L"rb");
#else
        return std::fopen((root/std::filesystem::path(name)).c_str(),"rb");
#endif
    },std::make_shared<LocalLoose>(root));
}
std::optional<wr::ReadOnlyFileInfo> mounted_resource_info(wr::Process& p,const std::filesystem::path& path){
    return p.state().read_only_files?p.state().read_only_files->info(logical(p,path)):std::nullopt;
}
bool mounted_resource_path(wr::Process& p,const std::filesystem::path& path){return p.state().read_only_files && p.state().read_only_files->owns_path(logical(p,path));}
std::shared_ptr<resource::Source> open_process_resource(wr::Process& p,const std::filesystem::path& path){
    const auto info=mounted_resource_info(p,path);const auto name=logical(p,path);
    if(!info){
        // Mounted PAK names belong to the provider. A missing logical archive
        // must not silently fall through to the unrelated private-save folder.
        if(p.state().read_only_files && archive_name(key(name)))throw resource::NotFound(missing_archive(p,name));
        if(mounted_resource_path(p,path))throw resource::NotFound("Loose resource is missing: "+name);
        if(auto* files=wr::mutable_files_for(p,path)){const auto info=files->info(name);if(!info)throw resource::NotFound("Cannot open selected-directory file: "+name);std::uint32_t error{};auto* file=files->open(name,true,false,3,error);if(!file)throw resource::Error("Cannot open selected-directory file: "+name+" (error "+std::to_string(error)+")");return std::make_shared<StreamSource>(file,info->size);}
        return open_resource_file(path);
    }
    if(info->attributes&16)throw resource::Error("resource archive path names a directory: "+name);
    std::FILE* file{};
    try{if(info->open_read){std::uint32_t error{};file=info->open_read(error);}else file=p.state().read_only_files->open(name);}
    catch(const resource::Error& error){throw resource::Error("cannot open mounted resource archive: "+name+" ("+error.what()+")");}
    if(!file){const auto failure=errno;throw resource::Error("cannot open mounted resource archive: "+name+
        (failure?" ("+std::error_code(failure,std::generic_category()).message()+")":""));}
    try{return std::make_shared<StreamSource>(file,info->size);}catch(...){std::fclose(file);throw;}
}
}
