#include "resource_catalog.hpp"
#include <algorithm>

namespace wa2::resource {
namespace {
std::string group_name(std::string_view group){
    if(group.empty())group="grp";
    if(group.size()>64)throw Error("resource group name exceeds limit");
    std::string result;result.reserve(group.size());
    for(unsigned char ch:group){
        if(!((ch>='a' && ch<='z') || (ch>='A' && ch<='Z') || (ch>='0' && ch<='9') || ch=='_' || ch=='-'))throw Error("invalid native resource group");
        result.push_back(char(ch>='A' && ch<='Z'?ch+32:ch));
    }
    return result;
}
void filename(std::string_view name){
    if(name.empty() || name.size()>255 || name.find('\0')!=std::string_view::npos || name.find('/')!=std::string_view::npos || name.find('\\')!=std::string_view::npos)
        throw Error("native resource must be an archive member filename");
}
}
Catalog::Catalog(BankOpener opener):opener_(std::move(opener)){if(!opener_)throw Error("missing native resource bank opener");}
Catalog::Bank& Catalog::bank(std::string_view path){
    if(auto found=banks_.find(path);found!=banks_.end())return found->second;
    Bank entry;if(auto source=opener_(path))entry.archive=Archive::open(std::move(source));
    return banks_.emplace(std::string(path),std::move(entry)).first->second;
}
const Archive& Catalog::required(std::string_view group){
    const auto name=group_name(group)+".pak";auto& selected=bank(name);
    if(!selected.archive)throw Error("missing original resource bank "+name);return *selected.archive;
}
const Archive* Catalog::optional(std::string_view group,bool introductory){
    const auto path=(introductory?"IC/":"")+group_name(group)+".pak";auto& selected=bank(path);return selected.archive?&*selected.archive:nullptr;
}
Catalog::Found Catalog::find(std::string_view group,std::string_view name){
    filename(name);const auto pack=group_name(group)+".pak";
    // Cached 0x437820 checks patch.pak, root PAK, then IC PAK. Keep that order
    // when both chapters contain a matching filename.
    for(const auto& path:std::array<std::string,3>{"patch.pak",pack,"IC/"+pack}){
        auto& selected=bank(path);if(!selected.archive)continue;
        const auto member=selected.archive->find_first(name);if(member!=Archive::missing)return {&selected,member};
    }
    throw Error("missing original resource "+std::string(name)+" in "+pack+"/IC");
}
std::shared_ptr<image::ImageRequest> Catalog::image(std::string_view group,std::string_view name){
    auto selected=find(group,name);if(!selected.bank->images)selected.bank->images=std::make_unique<image::ImageStore>(*selected.bank->archive);
    return selected.bank->images->request(selected.bank->archive->members()[selected.member].name);
}
std::shared_ptr<Source> Catalog::stream(std::string_view group,std::string_view name){const auto selected=find(group,name);return selected.bank->archive->open_member(selected.member);}
std::vector<std::uint8_t> Catalog::data(std::string_view group,std::string_view name){const auto selected=find(group,name);return selected.bank->archive->load(selected.member);}
std::shared_ptr<Source> Catalog::movie(std::string_view name){
    filename(name);std::string folded(name);for(auto& ch:folded)if(ch>='A' && ch<='Z')ch+=32;
    if(folded.size()<5 || folded.substr(folded.size()-4)!=".pak")throw Error("original movie must be a PAK file");
    if(auto found=movies_.find(folded);found!=movies_.end())return found->second;
    for(const auto& path:std::array<std::string,2>{folded,"IC/"+folded})if(auto source=opener_(path)){movies_[folded]=source;return source;}
    throw Error("missing original movie "+folded);
}
std::size_t Catalog::open_banks()const noexcept {return std::count_if(banks_.begin(),banks_.end(),[](const auto& item){return item.second.archive.has_value();});}
}
