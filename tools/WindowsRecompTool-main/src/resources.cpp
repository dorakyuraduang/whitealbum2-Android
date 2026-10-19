#include "winrecomp/resources.hpp"
#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>
namespace wr {
namespace {
[[noreturn]] void bad(const char* text){throw std::runtime_error(std::string("invalid PE resources: ")+text);}
struct Reader {
    const Image& image;Directory directory;
    std::span<const std::uint8_t> bytes(std::uint64_t relative,std::size_t length) const {
        if(relative>directory.size || length>std::uint64_t(directory.size)-relative)bad("directory range exceeds resource bounds");
        const auto va=std::uint64_t(image.base)+directory.rva+relative;
        if(va>0xffffffffull)bad("address wraps");
        const auto offset=image.offset(Address(va),length);if(!offset)bad("directory is not file backed");
        return std::span(image.bytes).subspan(*offset,length);
    }
    std::uint16_t word(std::uint64_t at) const {auto b=bytes(at,2);return std::uint16_t(b[0]|(unsigned(b[1])<<8));}
    std::uint32_t dword(std::uint64_t at) const {auto b=bytes(at,4);return std::uint32_t(b[0])|(std::uint32_t(b[1])<<8)|(std::uint32_t(b[2])<<16)|(std::uint32_t(b[3])<<24);}
    ResourceName name(std::uint32_t value) const {
        if(!(value&0x80000000u)){if(value>0xffff)bad("identifier exceeds 16 bits");return std::uint16_t(value);}
        const auto at=value&0x7fffffffu;const auto size=word(at);if(size>4096)bad("oversized name");
        bytes(std::uint64_t(at)+2,std::size_t(size)*2);std::u16string result;result.reserve(size);
        for(unsigned n=0;n<size;++n){auto ch=word(std::uint64_t(at)+2+2*n);if(!ch)bad("embedded NUL in name");result.push_back(char16_t(ch));}return result;
    }
};
}
std::vector<Resource> resource_directory(const Image& image) {
    const auto directory=image.directories[2];if(!directory.rva && !directory.size)return {};
    if(!directory.rva || directory.size<16)bad("incomplete root");Reader r{image,directory};
    std::vector<Resource> result;std::set<std::uint32_t> active;std::size_t visited=0;
    std::function<void(std::uint32_t,unsigned,ResourceName,ResourceName)> walk;
    walk=[&](std::uint32_t at,unsigned depth,ResourceName type,ResourceName name) {
        if(depth>2 || !active.insert(at).second)bad("directory cycle or excessive depth");
        r.bytes(at,16);const unsigned named=r.word(std::uint64_t(at)+12),ids=r.word(std::uint64_t(at)+14),count=named+ids;
        if(count>4096 || (visited+=count)>16384)bad("entry budget exceeded");
        r.bytes(std::uint64_t(at)+16,std::size_t(count)*8);std::set<ResourceName> keys;
        for(unsigned n=0;n<count;++n) {
            const auto pos=std::uint64_t(at)+16+8*n;const auto raw=r.dword(pos),target=r.dword(pos+4);
            if(bool(raw&0x80000000u)!=(n<named))bad("name/id partition mismatch");
            auto key=r.name(raw);if(!keys.insert(key).second)bad("duplicate directory key");
            if(depth<2) {
                if(!(target&0x80000000u))bad("leaf above language level");
                walk(target&0x7fffffffu,depth+1,depth==0?key:type,depth==1?key:name);
            } else {
                if(target&0x80000000u)bad("directory below language level");
                if(!std::holds_alternative<std::uint16_t>(key))bad("named language entry");
                r.bytes(target,16);Resource item{type,name,std::get<std::uint16_t>(key),r.dword(std::uint64_t(target)+8),r.dword(target),r.dword(std::uint64_t(target)+4)};
                if(r.dword(std::uint64_t(target)+12)!=0)bad("nonzero reserved data field");
                if(item.size>16u*1024*1024)bad("payload exceeds resource limit");
                if(item.size)resource_bytes(image,item);result.push_back(std::move(item));
            }
        }
        active.erase(at);
    };
    walk(0,0,std::uint16_t(0),std::uint16_t(0));return result;
}
std::span<const std::uint8_t> resource_bytes(const Image& image,const Resource& resource) {
    const auto address=std::uint64_t(image.base)+resource.rva;
    if(address>0xffffffffull || std::uint64_t(resource.rva)+resource.size>image.size)bad("payload address wraps or exceeds image");
    if(!resource.size)return {};
    const auto off=image.offset(Address(address),resource.size);if(!off)bad("payload is not file backed");
    return std::span(image.bytes).subspan(*off,resource.size);
}
std::optional<Resource> find_resource(const Image& image,const ResourceName& type,const ResourceName& name,std::uint16_t language) {
    auto entries=resource_directory(image);std::vector<Resource> matches;
    for(const auto& item:entries)if(item.type==type && item.name==name)matches.push_back(item);
    for(auto lang:{language,std::uint16_t(0)})for(const auto& item:matches)if(item.language==lang)return item;
    if(matches.size()==1)return matches.front();
    if(matches.size()>1)throw std::runtime_error("resource language selection outside the explicit en-US/neutral profile");
    return {};
}
}
