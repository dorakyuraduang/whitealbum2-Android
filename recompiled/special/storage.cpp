#include "runtime.hpp"
#include <winrecomp/path_name.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <sstream>

namespace wa2::special {namespace {
constexpr std::string_view special="Documents/Leaf/WHITE ALBUM2 Special Contents";
std::string fold(std::string_view value){return wr::normalize_windows_name(value,true);}
bool filename(std::string_view name){
    return name=="sys.sav" || name=="save_q.sav" || name=="system.ini" || name=="d3d.ini" || name=="default.ini" ||
        (name.size()==11 && name.starts_with("save_") && name.ends_with(".sav") && name[5]>='0' && name[5]<='9' && name[6]>='0' && name[6]<='9');
}
bool parent(std::string_view name){return name.empty() || name=="documents" || name=="documents/leaf";}
std::optional<std::string> path(std::string_view name,bool directory,bool writing=false){
    auto value=fold(name);const auto root=fold(special);
    if(value==".")value.clear();
    if(value.find('\0')!=value.npos || value.find(':')!=value.npos || value.find("..")!=value.npos || value.starts_with('/'))return {};
    if(directory){
        if(value.empty())return std::string{};
        if(value==root)return std::string(special);
        if(parent(value))return value=="documents"?"Documents":"Documents/Leaf";
        if(!writing && (value=="documents/leaf/white album2 -closing chapter-" || value=="documents/leaf/white album2 -introductory chapter-"))return std::string(name);
        return {};
    }
    const auto slash=value.find_last_of('/');
    const auto folder=slash==value.npos?std::string{}:value.substr(0,slash);
    const auto file=slash==value.npos?value:value.substr(slash+1);
    if(!filename(file))return {};
    const auto canonical=file=="sys.sav"?"Sys.sav":file=="save_q.sav"?"save_Q.sav":file;
    if(folder.empty() || folder==root)return std::string(special)+"/"+canonical;
    if(!writing && (folder=="documents/leaf/white album2 -closing chapter-" || folder=="documents/leaf/white album2 -introductory chapter-"))return std::string(name);
    return {};
}
class Files final:public wr::MutableFiles {
    std::shared_ptr<wr::MutableFiles> files_;
    struct Metric {std::uint64_t calls{},ns{},maximum{};};
    std::array<Metric,4> metrics_{};
    std::uint64_t unmapped_identities_{};
    std::string last_unmapped_identity_;
    struct Timer {
        Metric& metric;std::chrono::steady_clock::time_point start;
        explicit Timer(Metric& value):metric(value){const auto saved=errno;start=std::chrono::steady_clock::now();++metric.calls;errno=saved;}
        ~Timer(){const auto saved=errno;const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());metric.ns+=ns;metric.maximum=std::max(metric.maximum,ns);errno=saved;}
    };
public:
    explicit Files(std::shared_ptr<wr::MutableFiles> files):files_(std::move(files)){
        if(!files_)throw std::invalid_argument("missing Special storage adapter");
        for(const auto& folder:{std::string("Documents"),std::string("Documents/Leaf"),std::string(special)}){
            Timer timing(metrics_[3]);
            const auto error=files_->directory(folder);if(error && error!=183)throw std::runtime_error("Cannot prepare Special Contents save directory: "+std::to_string(error));
        }
    }
    std::optional<wr::ReadOnlyFileInfo> info(std::string_view name)override{
        Timer timing(metrics_[0]);
        auto mapped=path(name,false);if(!mapped)mapped=path(name,true);
        return mapped?files_->info(*mapped):std::nullopt;
    }
    std::string identity(std::string_view name)override{
        if(const auto mapped=path(name,false))return files_->identity(*mapped);
        // Identity is only a sharing key. CreateFile prepares it even when
        // info() found no file; Delete/Move also query it before access checks.
        // Unmapped probes must reach the normal Win32 error return. They never
        // reach the provider: open/remove/move retain their path allowlists.
        ++unmapped_identities_;last_unmapped_identity_=name.substr(0,256);
        return fold(name);
    }
    std::vector<wr::ReadOnlyFileInfo> list(std::string_view name)override{
        Timer timing(metrics_[1]);
        const auto mapped=path(name,true);if(!mapped)return {};
        auto entries=files_->list(mapped->empty()?special:*mapped);
        entries.erase(std::remove_if(entries.begin(),entries.end(),[](const auto& item){return !filename(fold(item.name));}),entries.end());
        return entries;
    }
    std::FILE* open(std::string_view name,bool read,bool write,std::uint32_t creation,std::uint32_t& error)override{
        Timer timing(metrics_[2]);
        const auto mapped=path(name,false,write || creation!=3);if(!mapped){error=5;return nullptr;}
        return files_->open(*mapped,read,write,creation,error);
    }
    std::uint32_t directory(std::string_view name)override{Timer timing(metrics_[3]);const auto mapped=path(name,true,true);if(!mapped)return 5;if(mapped->empty())return 183;return files_->directory(*mapped);}
    std::uint32_t remove(std::string_view name,bool directory)override{
        const auto mapped=path(name,directory,true);if(!mapped || directory)return 5;return files_->remove(*mapped,false);
    }
    std::uint32_t move(std::string_view from,std::string_view to)override{const auto a=path(from,false,true),b=path(to,false,true);return a && b?files_->move(*a,*b):5;}
    std::uint32_t attributes(std::string_view name,std::uint32_t flags)override{const auto mapped=path(name,false,true);return mapped?files_->attributes(*mapped,flags):5;}
    void replace(std::string_view name,std::span<const std::uint8_t> bytes)override{const auto mapped=path(name,false,true);if(!mapped)throw std::runtime_error("Unknown Special save/configuration path");files_->replace(*mapped,bytes);}
    std::string report()const{
        constexpr const char* names[]={"metadata","enumeration","descriptor_open","directory"};std::ostringstream out;
        out<<"{\"save_policy\":\"special-save-only\",\"root_save_fallback\":false,\"timing\":[";
        for(unsigned n=0;n<metrics_.size();++n){if(n)out<<',';const auto& m=metrics_[n];out<<"{\"stage\":\""<<names[n]<<"\",\"calls\":"<<m.calls<<",\"total_ns\":"<<m.ns<<",\"max_ns\":"<<m.maximum<<'}';}
        out<<"],\"unmapped_identity_queries\":"<<unmapped_identities_<<",\"last_unmapped_identity\":"<<wr::quote(last_unmapped_identity_)
            <<",\"maximum_logged_path_bytes\":256,\"payload_cached\":false,\"screen_access\":false}";return out.str();
    }
};
}
std::shared_ptr<wr::MutableFiles> save_files(std::shared_ptr<wr::MutableFiles> files){return std::make_shared<Files>(std::move(files));}
std::string save_files_report(const std::shared_ptr<wr::MutableFiles>& files){const auto* special=dynamic_cast<const Files*>(files.get());return special?special->report():"{\"available\":false}";}
}
