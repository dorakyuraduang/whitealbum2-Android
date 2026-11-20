#include "runtime.hpp"
#include "save_profile.hpp"
#include "../save_source.hpp"
#include "../../platform/compat/save_files.hpp"
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
class Files final:public wr::MutableFiles,public platform::compat::SaveFileState {
    std::shared_ptr<wr::MutableFiles> files_;
    std::shared_ptr<wr::MutableFiles> reads_;
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
    explicit Files(std::shared_ptr<wr::MutableFiles> files,wr::Process* process):files_(std::move(files)){
        if(!files_)throw std::invalid_argument("missing Special storage adapter");
        for(const auto& folder:{std::string("Documents"),std::string("Documents/Leaf"),std::string(special)}){
            Timer timing(metrics_[3]);
            const auto error=files_->directory(folder);if(error && error!=183)throw std::runtime_error("Cannot prepare Special Contents save directory: "+std::to_string(error));
        }
        namespace c=platform::compat;
        reads_=c::save_files(files_,false,
            process?aot::traced_save_read_scope(*process,save_source):c::SaveReadScope{},
            process?aot::save_index_read_ahead(*process,save_source):c::SaveReadAhead{},
            process?aot::save_list_metadata_read(*process,save_source):c::SaveListRead{},
            process?aot::save_page_preview_read_ahead(*process,save_source):c::SavePreviewRead{},
            {std::string(special),true});
    }
    std::optional<wr::ReadOnlyFileInfo> info(std::string_view name)override{
        Timer timing(metrics_[0]);
        auto mapped=path(name,false);if(!mapped)mapped=path(name,true);
        return mapped?reads_->info(*mapped):std::nullopt;
    }
    std::string identity(std::string_view name)override{
        if(const auto mapped=path(name,false))return reads_->identity(*mapped);
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
        auto entries=reads_->list(mapped->empty()?special:*mapped);
        entries.erase(std::remove_if(entries.begin(),entries.end(),[](const auto& item){return !filename(fold(item.name));}),entries.end());
        return entries;
    }
    std::FILE* open(std::string_view name,bool read,bool write,std::uint32_t creation,std::uint32_t& error)override{
        Timer timing(metrics_[2]);
        const auto mapped=path(name,false,write || creation!=3);if(!mapped){error=5;return nullptr;}
        return reads_->open(*mapped,read,write,creation,error);
    }
    OpenPlan prepare_open(std::string_view name)override{
        const auto mapped=path(name,false);if(!mapped)return wr::MutableFiles::prepare_open(name);
        Timer timing(metrics_[0]);auto plan=reads_->prepare_open(*mapped);
        const bool writable=path(name,false,true).has_value();auto opener=std::move(plan.open);
        plan.open=[this,opener=std::move(opener),writable](bool read,bool write,std::uint32_t creation,std::uint32_t& error){
            if(!writable && (write || creation!=3)){error=5;return static_cast<std::FILE*>(nullptr);}
            Timer timing(metrics_[2]);
            return opener(read,write,creation,error);
        };
        return plan;
    }
    std::uint32_t directory(std::string_view name)override{Timer timing(metrics_[3]);const auto mapped=path(name,true,true);if(!mapped)return 5;if(mapped->empty())return 183;return reads_->directory(*mapped);}
    std::uint32_t remove(std::string_view name,bool directory)override{
        const auto mapped=path(name,directory,true);if(!mapped || directory)return 5;return reads_->remove(*mapped,false);
    }
    std::uint32_t move(std::string_view from,std::string_view to)override{const auto a=path(from,false,true),b=path(to,false,true);return a && b?reads_->move(*a,*b):5;}
    std::uint32_t attributes(std::string_view name,std::uint32_t flags)override{const auto mapped=path(name,false,true);return mapped?reads_->attributes(*mapped,flags):5;}
    void replace(std::string_view name,std::span<const std::uint8_t> bytes)override{const auto mapped=path(name,false,true);if(!mapped)throw std::runtime_error("Unknown Special save/configuration path");reads_->replace(*mapped,bytes);}
    std::uint64_t activity()const override{return platform::compat::save_files_activity(reads_.get()).value_or(0);}
    std::uint64_t revision()const override{return platform::compat::save_files_revision(reads_.get()).value_or(0);}
    std::string report()const{
        constexpr const char* names[]={"metadata","enumeration","descriptor_open","directory"};std::ostringstream out;
        out<<"{\"save_policy\":\"special-save-only\",\"root_save_fallback\":false,\"timing\":[";
        for(unsigned n=0;n<metrics_.size();++n){if(n)out<<',';const auto& m=metrics_[n];out<<"{\"stage\":\""<<names[n]<<"\",\"calls\":"<<m.calls<<",\"total_ns\":"<<m.ns<<",\"max_ns\":"<<m.maximum<<'}';}
        out<<"],\"unmapped_identity_queries\":"<<unmapped_identities_<<",\"last_unmapped_identity\":"<<wr::quote(last_unmapped_identity_)
            <<",\"shared_inventory\":"<<platform::compat::save_files_report(reads_)
            <<",\"maximum_logged_path_bytes\":256,\"payload_cached\":false,\"screen_access\":false}";return out.str();
    }
};
}
std::shared_ptr<wr::MutableFiles> save_files(std::shared_ptr<wr::MutableFiles> files,wr::Process* process){return std::make_shared<Files>(std::move(files),process);}
std::string save_files_report(const std::shared_ptr<wr::MutableFiles>& files){const auto* special=dynamic_cast<const Files*>(files.get());return special?special->report():"{\"available\":false}";}
std::uint64_t save_files_activity(const std::shared_ptr<wr::MutableFiles>& files){const auto* special=dynamic_cast<const Files*>(files.get());return special?special->activity():0;}
}
