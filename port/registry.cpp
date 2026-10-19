// A private registry hive under the guest data root. Native HKCU/HKLM are never
// opened. ANSI values retain the exact byte count supplied by the guest.
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <memory>
#include "guest_api.hpp"
#include "wa2_win32.hpp"
namespace wr {
namespace {
using namespace port;
constexpr U32 ERROR_SUCCESS=0,ERROR_FILE_NOT_FOUND=2,ERROR_ACCESS_DENIED=5,ERROR_INVALID_HANDLE=6,
    ERROR_NOT_ENOUGH_MEMORY=8,ERROR_WRITE_FAULT=29,ERROR_INVALID_PARAMETER=87,ERROR_MORE_DATA=234;
constexpr U32 KEY_ALL_ACCESS=0xf003f,KEY_CREATE_SUB_KEY=4,KEY_QUERY_VALUE=1,KEY_SET_VALUE=2,KEY_WOW64_64KEY=0x100;
constexpr U32 REG_CREATED_NEW_KEY=1,REG_OPENED_EXISTING_KEY=2,REG_OPTION_NON_VOLATILE=0;
std::string fold(std::string name){for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));return name;}
struct Registry {
    struct Value {U32 type;std::vector<std::uint8_t> bytes;};
    using Values=std::map<std::string,Value>;
    std::map<std::string,Values> keys;
    struct Handle {std::string key;U32 access;};
    std::map<U32,Handle> handles;
    bool loaded{};
    std::optional<Handle> handle(U32 id) {
        switch(id){case 0x80000000:return Handle{"hkcr",KEY_ALL_ACCESS};case 0x80000001:return Handle{"hkcu",KEY_ALL_ACCESS};case 0x80000002:return Handle{"hklm",KEY_ALL_ACCESS};case 0x80000003:return Handle{"hku",KEY_ALL_ACCESS};case 0x80000005:return Handle{"hkcc",KEY_ALL_ACCESS};default:break;}
        auto it=handles.find(id);return it==handles.end()?std::nullopt:std::optional(it->second);
    }
    void load(Process& p) {
        if(loaded)return;
        const auto file=guest_file_path(p,"C:\\WinRecomp\\.wa2-runtime\\registry.bin");
        if(!std::filesystem::exists(file)){loaded=true;return;}
        if(std::filesystem::file_size(file)>16u*1024*1024)throw std::runtime_error("private registry exceeds size limit");
        std::ifstream in(file,std::ios::binary);std::array<char,8> magic{};in.read(magic.data(),8);
        if(std::string(magic.data(),8)!="WA2REG01")throw std::runtime_error("invalid private registry header");
        auto number=[&](){U32 n{};in.read(reinterpret_cast<char*>(&n),4);if(!in)throw std::runtime_error("truncated private registry");return n;};
        auto text=[&](){auto n=number();if(n>32768)throw std::runtime_error("private registry name limit");std::string s(n,'\0');in.read(s.data(),n);if(!in)throw std::runtime_error("truncated private registry name");return s;};
        std::map<std::string,Values> staged;const auto count=number();if(count>4096)throw std::runtime_error("private registry key limit");
        for(U32 n=0;n<count;++n){auto key=text();const auto values=number();if(values>4096)throw std::runtime_error("private registry value limit");
            auto& row=staged[key];for(U32 j=0;j<values;++j){auto name=text();const auto type=number(),size=number();if(size>1048576)throw std::runtime_error("private registry data limit");
                std::vector<std::uint8_t> bytes(size);in.read(reinterpret_cast<char*>(bytes.data()),size);if(!in)throw std::runtime_error("truncated private registry value");row.emplace(name,Value{type,std::move(bytes)});}}
        keys=std::move(staged);loaded=true;
    }
    U32 save(Process& p) {
        if(!p.options.allow_file_write)return ERROR_ACCESS_DENIED;
        const auto file=guest_file_path(p,"C:\\WinRecomp\\.wa2-runtime\\registry.bin");
        const auto temporary=guest_file_path(p,"C:\\WinRecomp\\.wa2-runtime\\registry.bin.tmp");
        std::error_code ec;std::filesystem::create_directories(file.parent_path(),ec);if(ec)return ERROR_ACCESS_DENIED;
        std::ofstream out(temporary,std::ios::binary|std::ios::trunc);if(!out)return ERROR_ACCESS_DENIED;
        auto number=[&](U32 n){out.write(reinterpret_cast<const char*>(&n),4);};
        auto text=[&](const std::string& s){number(U32(s.size()));out.write(s.data(),std::streamsize(s.size()));};
        out.write("WA2REG01",8);number(U32(keys.size()));
        for(const auto& [key,values]:keys){text(key);number(U32(values.size()));for(const auto& [name,value]:values){text(name);number(value.type);number(U32(value.bytes.size()));out.write(reinterpret_cast<const char*>(value.bytes.data()),std::streamsize(value.bytes.size()));}}
        out.close();if(!out)return ERROR_WRITE_FAULT;
        std::filesystem::rename(temporary,file,ec);return ec?ERROR_WRITE_FAULT:ERROR_SUCCESS;
    }
    U32 open(Process& p,Args a,bool create) {
        load(p);const auto parent=handle(a[0]);if(!parent)return ERROR_INVALID_HANDLE;
        const unsigned result_index=create?7:4,access_index=create?5:3;
        if(!a[result_index])return ERROR_INVALID_PARAMETER;p.memory.check(a[result_index],4,Memory::Write);p.memory.store(a[result_index],0,32);
        if((create && (a[2] || a[4]!=REG_OPTION_NON_VOLATILE || a[6])) || (!create && a[2]))return ERROR_INVALID_PARAMETER;
        if(a[access_index]&KEY_WOW64_64KEY)throw GuestFault(FaultKind::unsupported,p.cpu.eip,"private registry has only a 32-bit view");
        std::string name=a[1]?fold(p.read_string(a[1])):"";while(!name.empty() && name.back()=='\\')name.pop_back();
        const auto key=parent->key+(name.empty()?"":"\\"+name);const bool exists=key.find('\\')==std::string::npos || keys.contains(key);
        if(!exists && !create)return ERROR_FILE_NOT_FOUND;
        if(!exists){
            if(!(parent->access&KEY_CREATE_SUB_KEY))return ERROR_ACCESS_DENIED;
            if(!p.options.allow_file_write)return ERROR_ACCESS_DENIED;
            if(keys.size()>=4096)return ERROR_NOT_ENOUGH_MEMORY;
            const auto previous=keys;for(std::size_t i=key.find('\\');i!=std::string::npos;i=key.find('\\',i+1))keys.try_emplace(key.substr(0,i));keys.try_emplace(key);
            const auto error=save(p);if(error){keys=previous;return error;}
        }
        if(create && a[8]){p.memory.check(a[8],4,Memory::Write);p.memory.store(a[8],exists?REG_OPENED_EXISTING_KEY:REG_CREATED_NEW_KEY,32);}
        const auto token=p.state().next_handle;p.state().next_handle+=16;handles.emplace(token,Handle{key,a[access_index]});p.memory.store(a[result_index],token,32);return ERROR_SUCCESS;
    }
};
}
void install_wa2_registry(Process& p) {
    const auto state=std::make_shared<Registry>();
    api(p,"advapi32.dll","RegOpenKeyExA",5,[state](Process& q,Args a){return state->open(q,a,false);});
    api(p,"advapi32.dll","RegCreateKeyExA",9,[state](Process& q,Args a){return state->open(q,a,true);});
    api(p,"advapi32.dll","RegCloseKey",1,[state](Process&,Args a)->U32 {
        if(a[0]>=0x80000000u && a[0]<=0x80000005u)return ERROR_SUCCESS;
        return state->handles.erase(a[0])?ERROR_SUCCESS:ERROR_INVALID_HANDLE;
    });
    api(p,"advapi32.dll","RegQueryValueExA",6,[state](Process& q,Args a)->U32 {
        state->load(q);const auto h=state->handle(a[0]);if(!h)return ERROR_INVALID_HANDLE;if(!(h->access&KEY_QUERY_VALUE))return ERROR_ACCESS_DENIED;
        if(a[2] || (a[4] && !a[5]))return ERROR_INVALID_PARAMETER;
        const auto key=state->keys.find(h->key);if(key==state->keys.end())return ERROR_FILE_NOT_FOUND;
        const auto name=a[1]?fold(q.read_string(a[1])):"";const auto value=key->second.find(name);if(value==key->second.end())return ERROR_FILE_NOT_FOUND;
        if(a[3])q.memory.check(a[3],4,Memory::Write);
        U32 capacity=0;if(a[5]){q.memory.check(a[5],4,Memory::Read|Memory::Write);capacity=q.memory.load(a[5],32);}
        const auto size=U32(value->second.bytes.size());
        if(a[3])q.memory.store(a[3],value->second.type,32);if(a[5])q.memory.store(a[5],size,32);
        if(a[4]){if(capacity<size)return ERROR_MORE_DATA;q.memory.copy_in(a[4],value->second.bytes);}return ERROR_SUCCESS;
    });
    api(p,"advapi32.dll","RegSetValueExA",6,[state](Process& q,Args a)->U32 {
        state->load(q);const auto h=state->handle(a[0]);if(!h)return ERROR_INVALID_HANDLE;if(!(h->access&KEY_SET_VALUE) || !q.options.allow_file_write)return ERROR_ACCESS_DENIED;
        if(a[2] || a[5]>1048576 || (a[5] && !a[4]))return ERROR_INVALID_PARAMETER;
        const auto name=a[1]?fold(q.read_string(a[1])):"";std::vector<std::uint8_t> bytes(a[5]);q.memory.copy_out(a[4],bytes);
        const auto previous=state->keys;state->keys[h->key][name]=Registry::Value{a[3],std::move(bytes)};
        const auto error=state->save(q);if(error)state->keys=previous;return error;
    });
    api(p,"advapi32.dll","RegDeleteValueA",2,[state](Process& q,Args a)->U32 {
        state->load(q);const auto h=state->handle(a[0]);if(!h)return ERROR_INVALID_HANDLE;if(!(h->access&KEY_SET_VALUE) || !q.options.allow_file_write)return ERROR_ACCESS_DENIED;
        const auto key=state->keys.find(h->key);if(key==state->keys.end())return ERROR_FILE_NOT_FOUND;
        const auto name=a[1]?fold(q.read_string(a[1])):"";if(!key->second.contains(name))return ERROR_FILE_NOT_FOUND;
        const auto previous=state->keys;key->second.erase(name);const auto error=state->save(q);if(error)state->keys=previous;return error;
    });
}
}
