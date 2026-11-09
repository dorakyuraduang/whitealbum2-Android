#include "language_resources.hpp"
#include "resource_mount.hpp"
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <algorithm>
#include <array>
#include <map>

namespace wa2::platform::compat {
namespace {
std::string fold(std::string_view value){
    std::string name(value);for(auto& c:name){if(c=='\\')c='/';else if(c>='A' && c<='Z')c=char(c+32);}
    while(name.starts_with("./"))name.erase(0,2);return name;
}
class SelectedResources final:public wr::ReadOnlyFiles {
    std::shared_ptr<wr::ReadOnlyFiles> original_;
    struct Alias {std::string physical;wr::ReadOnlyFileInfo info;};
    std::map<std::string,Alias,std::less<>> aliases_;
    std::optional<resource::Archive> english_,base_script_,ic_script_;
    std::map<std::string,std::pair<std::uint32_t,std::uint32_t>,std::less<>> directories_;
public:
    explicit SelectedResources(std::shared_ptr<wr::ReadOnlyFiles> original):original_(std::move(original)){}
    void english(wr::Process& process){
        english_=resource::Archive::open(open_process_resource(process,process.options.data_root/"en.pak"));
        const auto members=english_->members();
        std::vector<std::pair<std::uint32_t,std::string>> markers;
        for(const auto& member:members)if(!member.stored_size && (member.name=="bak" || member.name=="grp" || member.name=="fnt" || member.name=="script"))markers.emplace_back(member.source_index,member.name);
        std::sort(markers.begin(),markers.end());
        for(std::size_t n=0;n<markers.size();++n){
            if(!directories_.emplace(markers[n].second,std::pair{markers[n].first+1,n+1<markers.size()?markers[n+1].first:std::uint32_t(members.size())}).second)throw resource::Error("Duplicate English PAK directory");
        }
        if(original_->info("script.pak"))base_script_=resource::Archive::open(open_process_resource(process,process.options.data_root/"script.pak"));
        if(original_->info("IC/script.pak"))ic_script_=resource::Archive::open(open_process_resource(process,process.options.data_root/"IC/script.pak"));
        if(!base_script_)throw resource::Error("English resources need the original script.pak for base script data");
    }
    std::optional<LanguageMember> member(std::string_view path,std::string_view name)const{
        if(!english_)return {};
        auto logical=fold(path);const bool ic=logical.starts_with("ic/");if(ic)logical.erase(0,3);
        const bool script=logical=="ck-gal.pak" || logical=="script.pak" || logical=="en.pak";
        const auto group=script?std::string_view("script"):logical=="bak.pak"?std::string_view("bak"):logical=="grp.pak"?std::string_view("grp"):std::string_view{};
        if(group.empty())return {};
        auto found=english_->find_first(name);
        if(const auto range=directories_.find(group);range!=directories_.end()){
            const auto entries=english_->members();
            while(found!=resource::Archive::missing && found<entries.size() && !resource::compare_names(entries[found].name,name)){
                if(entries[found].source_index>=range->second.first && entries[found].source_index<range->second.second)break;
                ++found;
            }
            if(found>=entries.size() || (found!=resource::Archive::missing && resource::compare_names(entries[found].name,name)))found=resource::Archive::missing;
        }else if(!directories_.empty() || !script)found=resource::Archive::missing;
        if(found!=resource::Archive::missing)return LanguageMember{&*english_,found,0x8000000000000001ull};
        if(!script)return {};
        const auto* base=ic?(ic_script_?&*ic_script_:nullptr):base_script_?&*base_script_:nullptr;
        return LanguageMember{base,base?base->find_first(name):resource::Archive::missing,ic?0x8000000000000003ull:0x8000000000000002ull};
    }
    void select(std::string_view physical,std::array<std::string_view,2> targets){
        if(physical.empty())return;
        const auto name=std::string(physical);
        const auto info=original_->info(name);
        if(!info || (info->attributes&16))throw resource::NotFound("Selected language PAK is missing: "+name);
        for(const auto target:targets)for(const auto prefix:{"","IC/"}){
            const auto logical=std::string(prefix)+std::string(target);
            // Preserve absent IC banks so a root selection does not fabricate
            // an introductory chapter or change the existing fallback order.
            if(*prefix && !original_->info(logical))continue;
            auto copy=*info;copy.name=logical;
            aliases_.insert_or_assign(fold(logical),Alias{name,std::move(copy)});
        }
    }
    std::optional<wr::ReadOnlyFileInfo> info(std::string_view path)const override{
        if(const auto it=aliases_.find(fold(path));it!=aliases_.end())return it->second.info;
        return original_->info(path);
    }
    std::vector<wr::ReadOnlyFileInfo> list(std::string_view path)const override{
        auto result=original_->list(path);auto directory=fold(path);if(directory==".")directory.clear();
        if(!directory.empty() && directory!="ic")return result;
        const auto prefix=directory.empty()?std::string{}:directory+"/";
        for(const auto& [name,alias]:aliases_){
            const auto slash=name.find_last_of('/');
            if((slash==std::string::npos?std::string{}:name.substr(0,slash+1))!=prefix)continue;
            const auto leaf=name.substr(slash==std::string::npos?0:slash+1);
            std::erase_if(result,[&](const auto& entry){return fold(entry.name)==leaf;});
            auto copy=alias.info;const auto at=copy.name.find_last_of('/');if(at!=std::string::npos)copy.name.erase(0,at+1);result.push_back(std::move(copy));
        }
        return result;
    }
    std::FILE* open(std::string_view path)override{
        if(const auto it=aliases_.find(fold(path));it!=aliases_.end())return original_->open(it->second.physical);
        return original_->open(path);
    }
};
void check_pack(wr::Process& process,const std::string& name,bool font){
    if(name.empty())return;
    try{
        const auto archive=resource::Archive::open(open_process_resource(process,process.options.data_root/name));
        // Only the index is read here, never a script/atlas payload. The game
        // keeps its own codecs and filename/character ordering unchanged.
        if(font){
            static constexpr std::array<std::string_view,4> names{
                "\x96\x7b\x91\xcc" "80.tga","\x91\xdc\x89\x65" "80.tga",
                "14pt\x96\x7b\x91\xcc.tga","14pt\x91\xdc.tga"};
            for(const auto member:names)if(archive.find_first(member)==resource::Archive::missing)
                throw resource::Error("Font PAK is missing a required original atlas; TTF/OTF fonts are not supported");
        }else{
            bool pair=false;
            for(const auto& member:archive.members())if(member.name.size()>4 && resource::compare_names(std::string_view(member.name).substr(member.name.size()-4),".bnr")==0){
                const auto text=member.name.substr(0,member.name.size()-4)+".txt";
                if(archive.find_first(text)!=resource::Archive::missing){pair=true;break;}
            }
            if(!pair)throw resource::Error("Script PAK must contain matching original .bnr and .txt members");
        }
    }catch(const resource::Error& error){throw resource::Error("Cannot use selected "+std::string(font?"font":"script")+" PAK "+name+": "+error.what());}
}
}
void select_language_resources(wr::Process& process,const resource::LanguageResources& selection){
    resource::validate_language_resources(selection);
    if(process.entry)throw std::logic_error("Language resources must be selected before image load");
    const auto profile=resource::language_profile(selection.language);
    {
        if(!process.state().read_only_files)throw resource::Error("Language PAK selection needs a mounted game directory");
        // Validate against the unmodified mount before publishing any alias.
        check_pack(process,std::string(profile.script),false);
        check_pack(process,std::string(profile.font),true);
        auto selected=std::make_shared<SelectedResources>(process.state().read_only_files);
        if(selection.language==resource::GameLanguage::english)selected->english(process);
        selected->select(profile.script,{"ck-gal.pak","script.pak"});
        selected->select(profile.font,{"fon.pak","fnt.pak"});
        install_resource_mount(process,std::move(selected));
    }
    process.state().reports["language_resources"]=[selection,profile]{return "{\"language\":"+wr::quote(std::string(profile.tag))+",\"script\":"+wr::quote(std::string(profile.script))+",\"font\":"+wr::quote(std::string(profile.font))+
        ",\"chinese_subtitles\":"+(selection.chinese_subtitles?"true":"false")+",\"patch_priority_preserved\":true,\"resource_payloads_copied\":false}";};
}
std::optional<LanguageMember> language_resource_member(wr::Process& process,const std::filesystem::path& path,std::string_view member){
    const auto* selected=dynamic_cast<const SelectedResources*>(process.state().read_only_files.get());if(!selected)return {};
    const auto relative=path.lexically_relative(process.options.data_root);if(relative.empty() || *relative.begin()=="..")return {};
    return selected->member(relative.generic_string(),member);
}
}
