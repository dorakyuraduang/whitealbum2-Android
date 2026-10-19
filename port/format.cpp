#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winrecomp/process.hpp>
#include <algorithm>
#include <array>
#include <cstdarg>
#include <deque>
#include <string>
#include <type_traits>
#include "format.hpp"

namespace wr {
U32 format_guest_ansi(Process& p,U32 destination,U32 format_address,U32 arguments) {
    static_assert(sizeof(void*)==8 && std::is_pointer_v<va_list>);
    const auto format=p.read_string(format_address,4096);
    std::array<std::uint64_t,64> host_arguments{};unsigned count=0;
    std::deque<std::string> strings;std::deque<std::wstring> wide_strings;
    std::uint64_t cursor=arguments;std::size_t maximum=0;
    auto read32=[&]()->U32 {
        if(cursor+4>0x100000000ull)throw GuestFault(FaultKind::memory,U32(cursor),"32-bit varargs list wraps");
        const auto value=p.memory.load(U32(cursor),32);cursor+=4;return value;
    };
    auto fault=[&](const char* detail){throw GuestFault(FaultKind::unsupported,p.cpu.eip,detail);};
    for(std::size_t at=0;at<format.size();) {
        if(format[at++]!='%'){++maximum;continue;}
        if(at<format.size() && format[at]=='%'){++at;++maximum;continue;}
        while(at<format.size() && (format[at]=='-' || format[at]=='#' || format[at]=='0'))++at;
        auto digits=[&](){unsigned value=0;while(at<format.size() && format[at]>='0' && format[at]<='9'){value=value*10+unsigned(format[at++]-'0');if(value>1023)fault("wvsprintf field exceeds 1023 bytes");}return value;};
        const auto width=digits();unsigned precision=1023;bool has_precision=false;
        if(at<format.size() && format[at]=='.'){++at;has_precision=true;precision=digits();}
        std::string length;
        if(at<format.size() && (format[at]=='h' || format[at]=='l' || format[at]=='w'))length+=format[at++];
        else if(format.compare(at,3,"I64")==0){length="I64";at+=3;}
        else if(format.compare(at,3,"I32")==0){length="I32";at+=3;}
        if(at>=format.size())fault("unterminated wvsprintf conversion");
        const auto type=format[at++];
        if(std::string("cCsSdiuxX").find(type)==std::string::npos)fault("wvsprintf conversion outside the checked Windows profile");
        if(count==host_arguments.size())fault("too many wvsprintf arguments");
        std::uint64_t value=read32();std::size_t field_max=32;
        if(type=='s' || type=='S') {
            if(length=="I64" || length=="I32")fault("integer length on wvsprintf string");
            const bool wide=length=="l" || length=="w" || (type=='S' && length!="h");
            if(wide) {
                const auto text=value?p.read_wstring(U32(value)):std::u16string(u"(null)");
                wide_strings.emplace_back(text.begin(),text.end());
                value=reinterpret_cast<std::uintptr_t>(wide_strings.back().c_str());field_max=text.size()*4;
                if(has_precision)field_max=std::min<std::size_t>(text.size(),precision)*4;
            }else{
                strings.push_back(value?p.read_string(U32(value)):std::string("(null)"));
                value=reinterpret_cast<std::uintptr_t>(strings.back().c_str());field_max=strings.back().size();
            }
            if(has_precision && !wide)field_max=std::min<std::size_t>(field_max,precision);
        }else if(type=='c' || type=='C') {
            if(length=="I64" || length=="I32")fault("integer length on wvsprintf character");
            field_max=4;
        }else{
            if(length=="w")fault("wide length on wvsprintf integer");
            if(length=="I64")value|=std::uint64_t(read32())<<32;
            if(has_precision)field_max=std::max<std::size_t>(field_max,precision+3);
        }
        host_arguments[count++]=value;maximum+=std::max<std::size_t>(width,field_max);
        if(maximum>=1024)fault("wvsprintf result exceeds its checked 1024-byte buffer");
    }
    if(maximum>=1024)fault("wvsprintf literal exceeds its checked buffer");
    std::array<char,1024> output{};
    const auto result=::wvsprintfA(output.data(),format.c_str(),reinterpret_cast<va_list>(host_arguments.data()));
    if(result<0 || result>=int(output.size()))fault("native wvsprintf result outside the checked buffer");
    p.memory.check(destination,std::size_t(result)+1,Memory::Write);
    p.memory.copy_in(destination,std::span(reinterpret_cast<const std::uint8_t*>(output.data()),std::size_t(result)+1));
    return U32(result);
}
}
