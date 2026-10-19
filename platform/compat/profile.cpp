#include "profile.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
namespace wa2::platform::compat {
namespace {
char fold(char c){return c>='A' && c<='Z'?char(c+32):c;}
bool equal(std::string_view a,std::string_view b){return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](char x,char y){return fold(x)==fold(y);});}
std::string_view trim(std::string_view text){
    const auto begin=text.find_first_not_of(" \t\r\n");if(begin==text.npos)return {};
    return text.substr(begin,text.find_last_not_of(" \t\r\n")-begin+1);
}
int nibble(char c){if(c>='0' && c<='9')return c-'0';c=fold(c);return c>='a' && c<='f'?c-'a'+10:-1;}
}
Profile Profile::read(const std::filesystem::path& path){
    Profile result;std::ifstream input(path,std::ios::binary);
    if(!input){std::error_code error;if(!std::filesystem::exists(path,error) && !error)return result;
        throw std::system_error(std::make_error_code(std::errc::io_error),"cannot read profile");}
    std::string line;Section* current=nullptr;std::size_t total{};
    while(std::getline(input,line)){
        total+=line.size()+1;if(total>1024*1024)throw std::runtime_error("profile exceeds one MiB");
        if(total==line.size()+1 && line.starts_with("\xff\xfe"))throw std::runtime_error("UTF16 profile requires an explicit encoding adapter");
        const auto text=trim(line);if(text.empty() || text.front()==';')continue;
        if(text.front()=='['){const auto end=text.find(']');if(end==text.npos)continue;
            const auto name=trim(text.substr(1,end-1));
            auto section=std::find_if(result.sections_.begin(),result.sections_.end(),[&](const auto& s){return equal(s.name,name);});
            if(section==result.sections_.end()){result.sections_.push_back({std::string(name),{}});current=&result.sections_.back();}else current=&*section;
            continue;}
        const auto separator=text.find('=');if(!current || separator==text.npos)continue;
        const auto key=trim(text.substr(0,separator));auto value=trim(text.substr(separator+1));
        if(value.size()>=2 && (value.front()=='\'' || value.front()=='"') && value.back()==value.front())value=value.substr(1,value.size()-2);
        if(std::none_of(current->entries.begin(),current->entries.end(),[&](const auto& e){return equal(e.key,key);}))
            current->entries.push_back({std::string(key),std::string(value)});
    }
    if(input.bad())throw std::system_error(std::make_error_code(std::errc::io_error),"short profile read");return result;
}
std::optional<std::string> Profile::get(std::string_view section,std::string_view key) const{
    for(const auto& s:sections_)if(equal(s.name,section))for(const auto& e:s.entries)if(equal(e.key,key))return e.value;return {};
}
std::vector<std::string> Profile::sections() const{std::vector<std::string> names;for(const auto& s:sections_)names.push_back(s.name);return names;}
std::vector<std::string> Profile::keys(std::string_view section) const{std::vector<std::string> names;for(const auto& s:sections_)if(equal(s.name,section))for(const auto& e:s.entries)names.push_back(e.key);return names;}
void Profile::set(std::string section,std::optional<std::string> key,std::optional<std::string> value){
    auto s=std::find_if(sections_.begin(),sections_.end(),[&](const auto& s){return equal(s.name,section);});
    if(!key){if(s!=sections_.end())sections_.erase(s);return;}
    if(s==sections_.end()){if(!value)return;sections_.push_back({std::move(section),{}});s=std::prev(sections_.end());}
    auto e=std::find_if(s->entries.begin(),s->entries.end(),[&](const auto& e){return equal(e.key,*key);});
    if(!value){if(e!=s->entries.end())s->entries.erase(e);return;}
    // Embedded line separators would change profile structure on reload.
    for(const auto* text:{&s->name,&*key,&*value})if(text->find_first_of("\r\n\0",0,3)!=std::string::npos)throw std::runtime_error("invalid profile text");
    if(e==s->entries.end())s->entries.push_back({std::move(*key),std::move(*value)});else e->value=std::move(*value);
}
void Profile::save(const std::filesystem::path& path) const{
    static std::atomic<std::uint64_t> serial{};auto temporary=path;temporary+=".wa2-"+std::to_string(++serial)+".tmp";
    try{
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);if(!output)throw std::runtime_error("cannot create profile temporary file");
        for(const auto& s:sections_){output<<'['<<s.name<<"]\r\n";for(const auto& e:s.entries)output<<e.key<<'='<<e.value<<"\r\n";output<<"\r\n";}
        output.flush();if(!output)throw std::runtime_error("cannot write profile");output.close();if(!output)throw std::runtime_error("cannot close profile");
        std::filesystem::rename(temporary,path);
    }catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}
std::string encode_profile_struct(std::span<const std::uint8_t> bytes){
    constexpr char digits[]="0123456789ABCDEF";std::string text;std::uint8_t sum{};text.reserve((bytes.size()+1)*2);
    auto append=[&](unsigned b){text+=digits[b>>4];text+=digits[b&15];};for(auto b:bytes){append(b);sum=std::uint8_t(sum+b);}append(sum);return text;
}
bool decode_profile_struct(std::string_view text,std::span<std::uint8_t> bytes){
    if(bytes.size()>(std::numeric_limits<std::size_t>::max()-2)/2 || text.size()!=(bytes.size()+1)*2)return false;
    // Validate first: short/corrupt values must leave the caller's memory intact.
    std::uint8_t sum{};for(std::size_t i=0;i<bytes.size()+1;++i){const auto hi=nibble(text[i*2]),lo=nibble(text[i*2+1]);if(hi<0 || lo<0)return false;
        const auto value=std::uint8_t(hi*16+lo);if(i==bytes.size()){if(value!=sum)return false;}else sum=std::uint8_t(sum+value);}
    for(std::size_t i=0;i<bytes.size();++i)bytes[i]=std::uint8_t(nibble(text[i*2])*16+nibble(text[i*2+1]));return true;
}
std::uint32_t profile_integer(std::string_view text,std::uint32_t fallback){
    text=trim(text);if(text.empty())return fallback;bool negative=false;
    if(text.front()=='+' || text.front()=='-'){negative=text.front()=='-';text.remove_prefix(1);}
    int base=10;if(text.starts_with("0x") || text.starts_with("0X")){base=16;text.remove_prefix(2);}
    std::uint32_t value{};const auto result=std::from_chars(text.data(),text.data()+text.size(),value,base);
    if(result.ptr==text.data() || result.ec!=std::errc{})return 0;return negative?0u-value:value;
}
}
