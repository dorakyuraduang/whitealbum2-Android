#include "profile.hpp"
#include "save_files.hpp"
#include "publish_file.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <system_error>
#include <sstream>
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
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
    std::ifstream input(path,std::ios::binary);
    if(!input){std::error_code error;if(!std::filesystem::exists(path,error) && !error)return {};
        throw std::system_error(std::make_error_code(std::errc::io_error),"cannot read profile");}
    return parse(input);
}
Profile Profile::read(std::span<const std::uint8_t> bytes){
    if(bytes.size()>1024*1024)throw std::runtime_error("profile exceeds one MiB");
    std::istringstream input(std::string(bytes.begin(),bytes.end()));return parse(input);
}
Profile Profile::parse(std::istream& input){
    Profile result;
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
        publish_closed_file(temporary,path);
    }catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}
std::vector<std::uint8_t> Profile::bytes()const{
    std::ostringstream output;for(const auto& s:sections_){output<<'['<<s.name<<"]\r\n";for(const auto& e:s.entries)output<<e.key<<'='<<e.value<<"\r\n";output<<"\r\n";}
    const auto text=output.str();if(text.size()>1024*1024)throw std::runtime_error("profile exceeds one MiB");return {text.begin(),text.end()};
}
Profile read_profile(wr::Process& process,const std::filesystem::path& path){
    auto* files=wr::mutable_files_for(process,path);if(!files)return Profile::read(path);
    const auto name=path.lexically_relative(process.options.data_root).generic_string();std::uint32_t error{};auto* file=files->open(name,true,false,3,error);
    if(!file){if(error==2 || error==3)return {};throw std::runtime_error("Cannot read selected-directory profile: "+name);}
    std::unique_ptr<std::FILE,decltype(&std::fclose)> stream(file,std::fclose);std::vector<std::uint8_t> bytes(1024*1024+1);const auto count=std::fread(bytes.data(),1,bytes.size(),file);if(std::ferror(file))throw std::runtime_error("Cannot finish selected-directory profile read: "+name);bytes.resize(count);return Profile::read(bytes);
}
void save_profile(wr::Process& process,const Profile& profile,const std::filesystem::path& path){
    auto* files=wr::mutable_files_for(process,path);if(!files){profile.save(path);return;}
    files->replace(path.lexically_relative(process.options.data_root).generic_string(),profile.bytes());
}
struct ProfileReader::State {
    struct Entry {Profile profile;std::shared_ptr<wr::MutableFiles> files;std::uint64_t scope{},revision{},used{};std::chrono::steady_clock::time_point expires;};
    ProfileReadScope scope;
    std::map<std::filesystem::path,Entry> entries;
    std::uint64_t reads{},hits{},loads{},load_ns{},maximum_load_ns{},serial{},declines{};
};
ProfileReader::ProfileReader(ProfileReadScope scope):state_(std::make_shared<State>()){state_->scope=std::move(scope);}
Profile ProfileReader::read(wr::Process& process,const std::filesystem::path& path){
    auto& state=*state_;++state.reads;
    const auto load=[&]{
        const auto start=std::chrono::steady_clock::now();++state.loads;
        struct Timer {State& state;std::chrono::steady_clock::time_point start;~Timer(){const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());state.load_ns+=ns;state.maximum_load_ns=std::max(state.maximum_load_ns,ns);}} timer{state,start};
        return read_profile(process,path);
    };
    const auto scope=state.scope?state.scope(process):std::nullopt;
    const auto* files=wr::mutable_files_for(process,path);const auto revision=save_files_revision(files);
    if(!scope || !revision){state.entries.clear();return load();}
    const auto now=std::chrono::steady_clock::now();auto found=state.entries.find(path);
    if(found!=state.entries.end()){
        auto& entry=found->second;
        if(entry.files.get()==files && entry.scope==*scope && entry.revision==*revision && now<entry.expires){++state.hits;entry.used=++state.serial;return entry.profile;}
        state.entries.erase(found);
    }
    auto profile=load();
    // Keep unusual/large INIs on the original path rather than retaining
    // potentially large parsed string tables on lower-memory devices.
    if(profile.bytes().size()>256*1024){++state.declines;return profile;}
    if(state.entries.size()>=4){auto oldest=std::min_element(state.entries.begin(),state.entries.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});state.entries.erase(oldest);}
    state.entries.emplace(path,State::Entry{profile,process.state().mutable_files,*scope,*revision,++state.serial,std::chrono::steady_clock::now()+std::chrono::milliseconds(250)});
    return profile;
}
void ProfileReader::clear(){state_->entries.clear();}
std::string ProfileReader::report()const{
    const auto& s=*state_;return "{\"reads\":"+std::to_string(s.reads)+",\"snapshot_hits\":"+std::to_string(s.hits)+",\"physical_loads\":"+std::to_string(s.loads)+",\"physical_load_ns\":"+std::to_string(s.load_ns)+",\"maximum_physical_load_ns\":"+std::to_string(s.maximum_load_ns)+",\"large_profile_declines\":"+std::to_string(s.declines)+",\"cached_profiles\":"+std::to_string(s.entries.size())+",\"maximum_age_ms\":250,\"maximum_profiles\":4,\"writes_deferred\":false}";
}
struct ProfileWriter::State {
    struct Entry {Profile profile;std::filesystem::path path;std::shared_ptr<wr::MutableFiles> files;std::uint64_t scope{},revision{};std::chrono::steady_clock::time_point expires;};
    ProfileWriteScope scope;std::optional<Entry> entry;
    std::uint64_t calls{},hits{},loads{},load_ns{},commits{},commit_ns{},maximum_commit_ns{},unchanged{},failures{},total_ns{};
};
ProfileWriter::ProfileWriter(ProfileWriteScope scope):state_(std::make_shared<State>()){state_->scope=std::move(scope);}
void ProfileWriter::clear(){state_->entry.reset();}
void ProfileWriter::write(wr::Process& process,const std::filesystem::path& path,std::string section,std::optional<std::string> key,std::optional<std::string> value){
    auto& state=*state_;++state.calls;const auto start=std::chrono::steady_clock::now();
    struct Timer {std::uint64_t& total;std::chrono::steady_clock::time_point begin=std::chrono::steady_clock::now();~Timer(){total+=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count());}} total{state.total_ns};
    try{
        const auto scope=state.scope?state.scope(process):std::nullopt;
        auto* files=wr::mutable_files_for(process,path);const auto revision=save_files_revision(files);
        const bool admitted=scope && revision && key && value;
        auto expires=start+std::chrono::milliseconds(250);Profile profile;
        if(admitted && state.entry && state.entry->path==path && state.entry->files.get()==files && state.entry->scope==*scope && state.entry->revision==*revision && start<state.entry->expires){
            profile=state.entry->profile;expires=state.entry->expires;++state.hits;
        }else{state.entry.reset();++state.loads;Timer loading{state.load_ns};profile=read_profile(process,path);}
        const bool unchanged=admitted && profile.get(section,*key)==value;
        profile.set(std::move(section),std::move(key),std::move(value));
        if(unchanged)++state.unchanged;
        else{
            const auto begin=std::chrono::steady_clock::now();++state.commits;
            try{Timer committing{state.commit_ns};save_profile(process,profile,path);}
            catch(...){state.maximum_commit_ns=std::max(state.maximum_commit_ns,std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count()));throw;}
            state.maximum_commit_ns=std::max(state.maximum_commit_ns,std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count()));
            expires=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
        }
        state.entry.reset();
        if(admitted && profile.bytes().size()<=256*1024)if(const auto current=save_files_revision(files))
            state.entry.emplace(State::Entry{std::move(profile),path,process.state().mutable_files,*scope,*current,expires});
    }catch(...){state.entry.reset();++state.failures;throw;}
}
std::string ProfileWriter::report()const{
    const auto& s=*state_;return "{\"calls\":"+std::to_string(s.calls)+",\"snapshot_hits\":"+std::to_string(s.hits)+",\"physical_loads\":"+std::to_string(s.loads)+",\"physical_load_ns\":"+std::to_string(s.load_ns)+",\"commit_attempts\":"+std::to_string(s.commits)+",\"commit_ns\":"+std::to_string(s.commit_ns)+",\"maximum_commit_ns\":"+std::to_string(s.maximum_commit_ns)+",\"unchanged_source_writes_avoided\":"+std::to_string(s.unchanged)+",\"failures\":"+std::to_string(s.failures)+",\"total_ns\":"+std::to_string(s.total_ns)+",\"maximum_age_ms\":250,\"maximum_profiles\":1,\"maximum_profile_bytes\":262144,\"writes_deferred\":false}";
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
