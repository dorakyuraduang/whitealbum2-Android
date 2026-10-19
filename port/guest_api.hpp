#pragma once
// Legacy ABI serialization only; native engine code does not include this.
#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include <array>
#include <cstring>
#include <optional>
#include <type_traits>
#include <vector>
namespace wr::port {
using Args=std::span<const U32>;
inline void api(Process& p,const char* dll,const std::string& name,unsigned count,std::function<U32(Process&,Args)> fn,bool caller_cleans_stack=false) {
    if(count>32)throw std::runtime_error("port argument limit");
    p.register_api(dll,name,count,[count,fn=std::move(fn)](Process& q){
        const auto snapshot=q.argument_snapshot();if(snapshot.size()==count)return fn(q,snapshot);
        std::array<U32,32> args{};for(unsigned n=0;n<count;++n)args[n]=q.argument(n);
        return fn(q,std::span(args).first(count));
    },caller_cleans_stack);
}
template<class T>T read(Process& p,U32 at) {
    static_assert(std::is_trivially_copyable_v<T>);T value{};
    p.memory.copy_out(at,std::span(reinterpret_cast<std::uint8_t*>(&value),sizeof(value)));return value;
}
template<class T>void write(Process& p,U32 at,const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    p.memory.copy_in(at,std::span(reinterpret_cast<const std::uint8_t*>(&value),sizeof(value)));
}
inline void string(Process& p,U32 at,const std::string& value){p.memory.copy_in(at,std::span(reinterpret_cast<const std::uint8_t*>(value.c_str()),value.size()+1));}
inline std::optional<std::string> nullable_string(Process& p,U32 at){return at?std::optional(p.read_string(at)):std::nullopt;}
inline const char* pointer(const std::optional<std::string>& value){return value?value->c_str():nullptr;}
}
