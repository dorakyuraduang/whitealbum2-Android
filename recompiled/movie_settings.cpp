#include "movie_settings.hpp"
#include "../platform/compat/profile.hpp"
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <array>
#include <map>
#include <system_error>
namespace wa2::aot {
namespace {
std::optional<unsigned> suffix(std::uint32_t level){
    switch(level){case 1:return 1;case 2:return 0;case 0:return 2;default:return {};}
}
}
std::string MovieSettings::report() const {
    return "{\"movie_groups\":"+std::to_string(groups)+",\"complete_variant_mask\":"+std::to_string(available_variants)+
        ",\"previous_mov_lv\":"+(previous?std::to_string(*previous):"null")+",\"selected_mov_lv\":"+(selected?std::to_string(*selected):"null")+
        ",\"changed\":"+(changed?"true":"false")+",\"reason\":"+wr::quote(reason)+",\"profile_file\":"+wr::quote(profile_file)+"}";
}
MovieSettings configure_movie_resources(wr::Process& p,const std::filesystem::path& path){
    std::map<std::string,unsigned> groups;
    const auto add=[&](std::string_view directory,std::string name){
        for(auto& c:name)if(c>='A' && c<='Z')c=char(c+32);
        if(name.size()!=9 || !name.starts_with("mv") || !name.ends_with(".pak") ||
           name[2]<'0' || name[2]>'9' || name[3]<'0' || name[3]>'9' || name[4]<'0' || name[4]>'2')return;
        groups[std::string(directory)+"/"+name.substr(0,4)]|=1u<<(name[4]-'0');
    };
    for(const auto* directory:{"","IC"}){
        if(p.state().read_only_files){
            for(const auto& file:p.state().read_only_files->list(directory))if(!(file.attributes&16))add(directory,file.name);
        }else{
            std::error_code error;const auto path=p.options.data_root/directory;
            if(!std::filesystem::exists(path,error)){if(error)throw std::system_error(error,"cannot inspect movie directory");continue;}
            for(const auto& file:std::filesystem::directory_iterator(path))if(file.is_regular_file())add(directory,file.path().filename().string());
        }
    }
    MovieSettings result;result.groups=groups.size();const auto utf8=path.generic_u8string();result.profile_file.assign(utf8.begin(),utf8.end());
    if(groups.empty()){result.reason="no-movie-files-indexed";return result;}
    result.available_variants=7;for(const auto& [name,mask]:groups)result.available_variants&=mask;
    auto profile=platform::compat::Profile::read(path);
    if(const auto text=profile.get("DEFAULT","mov_lv"))result.previous=platform::compat::profile_integer(*text,0xffffffffu);
    if(result.previous)if(const auto variant=suffix(*result.previous);variant && (result.available_variants&(1u<<*variant))){
        result.selected=result.previous;result.reason="preserved-available-setting";return result;
    }
    for(const auto level:std::array<std::uint32_t,3>{2,1,0})if(result.available_variants&(1u<<*suffix(level))){result.selected=level;break;}
    if(!result.selected){result.reason="no-complete-movie-variant";return result;}
    if(!p.options.allow_file_write){result.selected.reset();result.reason="profile-write-disabled";return result;}
    profile.set("DEFAULT","mov_lv",std::to_string(*result.selected));profile.save(path);result.changed=true;
    result.reason=result.previous?"replaced-unavailable-setting":"selected-from-external-movie-files";return result;
}
void prepare_movie_profile(wr::Process& p,const std::filesystem::path& path,std::string_view section,std::string_view key){
    const auto same=[](std::string_view a,std::string_view b){if(a.size()!=b.size())return false;for(std::size_t n=0;n<a.size();++n){const auto c=a[n]>='A' && a[n]<='Z'?char(a[n]+32):a[n];if(c!=b[n])return false;}return true;};
    if(!same(section,"default") || !same(key,"mov_lv"))return;
    // The caller supplies the path after the original game selected its save
    // folder and SYSTEM.ini. Do not guess default.ini or a host document path.
    const auto result=configure_movie_resources(p,path);
    p.state().reports["movie_resource_settings"]=[result]{return result.report();};
}
}
