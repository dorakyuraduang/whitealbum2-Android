#include <winrecomp/process.hpp>
#include <winrecomp/win32_state.hpp>
#include "../platform/compat/system.hpp"
#include "../platform/compat/profile.hpp"
#include "../platform/compat/text.hpp"
#include "../platform/compat/codepage.hpp"
#include "../platform/compat/locale.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
using namespace wr;
Image fixture_image(){
    Image image;image.base=0x400000;image.size=0x10000;image.headers_size=4096;image.entry=0x401000;image.bytes.resize(12288);
    image.sections={{".text",4096,4096,4096,4096,0x60000020},{".rsrc",8192,4096,8192,4096,0x40000040}};image.directories[2]={8192,256};
    auto word=[&](unsigned at,U32 value,unsigned size=4){for(unsigned n=0;n<size;++n)image.bytes[8192+at+n]=std::uint8_t(value>>(n*8));};
    word(14,1,2);word(16,10);word(20,0x80000018);word(24+14,1,2);word(40,1);word(44,0x80000030);
    word(48+14,1,2);word(64,0x804);word(68,72);word(72,0x2088);word(76,4);word(136,0x1234fedc);return image;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);const auto root=std::filesystem::path(argv[1]).parent_path()/("system-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(std::filesystem::create_directory(root));ProcessOptions options;options.data_root=root;options.allow_file_write=true;
    Process process(FunctionExecutor{[](Cpu&,Memory&){return false;}},options);wa2::platform::compat::install_system(process);wa2::platform::compat::install_text(process);wa2::platform::compat::install_locale(process);process.load(fixture_image());
    auto& m=process.memory;const auto work=process.allocate_bytes(4096);
    auto call=[&](const char* dll,const char* name,std::initializer_list<U32> args){const auto target=process.resolve(dll,name);CHECK(target);return process.callback(target,{args.begin(),args.size()});};
    auto kernel=[&](const char* name,std::initializer_list<U32> args){return call("kernel32.dll",name,args);};
    unsigned string_cases{};const auto strings=process.allocate_bytes(36864);std::vector<std::uint8_t> letters(32768,'a');
    for(unsigned length:{0u,1u,31u,4094u,4095u,4096u,8193u,32767u}){
        m.copy_in(strings,letters);m.store(strings+length,0,8);
        CHECK(process.string_length(strings)==length && process.read_string(strings).size()==length && kernel("lstrlenA",{strings})==length);++string_cases;
    }
    m.copy_in(strings,letters);bool bounded{};try{process.string_length(strings);}catch(const GuestFault& fault){bounded=fault.kind==FaultKind::memory && fault.address==strings;}CHECK(bounded);++string_cases;
    m.store(strings+4095,0,8);m.protect(strings+4096,4096,0);
    CHECK(process.string_length(strings+4095)==0 && process.read_string(strings+4095).empty());++string_cases;
    m.store(strings+4095,'b',8);
    for(bool length_only:{false,true}){bool rejected{};try{if(length_only)process.string_length(strings+4095);else process.read_string(strings+4095);}catch(const GuestFault& fault){rejected=fault.kind==FaultKind::memory && fault.address==strings+4096;}CHECK(rejected);++string_cases;}
    m.map(0xfffff000u,4096,Memory::Read|Memory::Write);m.store(0xffffffffu,0,8);
    CHECK(process.string_length(0xffffffffu)==0 && process.read_string(0xffffffffu).empty());++string_cases;
    m.store(0xffffffffu,'b',8);for(bool length_only:{false,true}){bool rejected{};try{if(length_only)process.string_length(0xffffffffu);else process.read_string(0xffffffffu);}catch(const GuestFault& fault){rejected=fault.kind==FaultKind::memory && fault.address==0xffffffffu;}CHECK(rejected);++string_cases;}
    CHECK(kernel("lstrlenA",{0})==0);++string_cases;
    const auto file=process.put_string("settings.ini"),section=process.put_string("Display"),key=process.put_string("Width"),value=process.put_string("1920");
    CHECK(kernel("WritePrivateProfileStringA",{section,key,value,file})==1);
    CHECK(kernel("GetPrivateProfileIntA",{section,key,7,file})==1920);
    const auto changed=process.put_string("1280");CHECK(kernel("WritePrivateProfileStringA",{section,key,changed,file})==1);
    CHECK(kernel("GetPrivateProfileStringA",{section,key,0,work,3,file})==2);CHECK(process.read_string(work)=="12");
    CHECK(kernel("GetPrivateProfileStringA",{0,0,0,work,128,file})==8);CHECK(process.read_string(work)=="Display" && m.load(work+8,8)==0);
    CHECK(kernel("GetPrivateProfileStringA",{section,0,0,work,128,file})==6);CHECK(process.read_string(work)=="Width" && m.load(work+6,8)==0);
    m.store(work,0x00ff12ab,32);const auto blob=process.put_string("Blob");CHECK(kernel("WritePrivateProfileStructA",{section,blob,work,4,file})==1);
    m.store(work,0,32);CHECK(kernel("GetPrivateProfileStructA",{section,blob,work,4,file})==1);CHECK(m.load(work,32)==0x00ff12ab);
    const auto bad=process.put_string("AB12FF0000");CHECK(kernel("WritePrivateProfileStringA",{section,blob,bad,file})==1);
    m.store(work,0x11223344,32);CHECK(kernel("GetPrivateProfileStructA",{section,blob,work,4,file})==0);CHECK(m.load(work,32)==0x11223344 && process.last_error==13);
    const auto fmt=process.put_string("%08X|%-4s|%d|%I64u|%%"),str=process.put_string("ok");
    for(auto [offset,arg]:{std::pair{0u,0x1234abu},{4u,str},{8u,0xfffffff9u},{12u,0u},{16u,1u}})m.store(work+512+offset,arg,32);
    CHECK(call("user32.dll","wvsprintfA",{work,fmt,work+512})==29);CHECK(process.read_string(work)=="001234AB|ok  |-7|4294967296|%");
    const auto string_format=process.put_string("%s");m.store(work+512,str,32);
    CHECK(call("user32.dll","wvsprintfA",{work,string_format,work+512})==2 && process.read_string(work)=="ok");
    m.store(work+512,0,32);CHECK(call("user32.dll","wvsprintfA",{work,string_format,work+512})==6 && process.read_string(work)=="(null)");
    CHECK(kernel("GetSystemInfo",{work})==0 && m.load(work+4,32)==4096 && m.load(work+24,32)==586);
    const auto resource=kernel("FindResourceA",{0,1,10});CHECK(resource);
    CHECK(kernel("SizeofResource",{0,resource})==4);const auto loaded=kernel("LoadResource",{0,resource});CHECK(loaded);
    CHECK(kernel("LockResource",{loaded})==loaded && m.load(loaded,32)==0x1234fedc);CHECK(kernel("FreeResource",{loaded})==0);
    CHECK(kernel("LoadResource",{0,resource})==loaded);CHECK(kernel("FindResourceA",{0,2,10})==0 && process.last_error==1814);
    CHECK(call("winmm.dll","timeEndPeriod",{1})==97);CHECK(call("winmm.dll","timeBeginPeriod",{1})==0);CHECK(call("winmm.dll","timeEndPeriod",{1})==0);
    CHECK(call("winmm.dll","timeGetDevCaps",{work,8})==0 && m.load(work,32)==1 && m.load(work+4,32)==1000);
    const std::array<std::uint8_t,5> chinese{0xd6,0xd0,0xce,0xc4,0};m.copy_in(work+256,chinese);
    CHECK(kernel("MultiByteToWideChar",{0,0,work+256,0xffffffff,work,16})==3 && process.read_wstring(work)==u"\u4e2d\u6587");
    CHECK(kernel("WideCharToMultiByte",{0,0,work,0xffffffff,work+128,16,0,work+512})==5);
    std::array<std::uint8_t,5> returned{};m.copy_out(work+128,returned);CHECK(returned==chinese && m.load(work+512,32)==0);
    const std::array<std::uint8_t,2> japanese{0x82,0xa0};m.copy_in(work+256,japanese);
    CHECK(kernel("MultiByteToWideChar",{932,8,work+256,2,work,1})==1 && m.load(work,16)==0x3042);
    const std::array<std::uint8_t,4> emoji{0xf0,0x9f,0x98,0x80};m.copy_in(work+256,emoji);
    CHECK(kernel("MultiByteToWideChar",{65001,8,work+256,4,work,2})==2 && m.load(work,32)==0xde00d83d);
    m.store(work,0xd83d,16);CHECK(kernel("WideCharToMultiByte",{65001,0x80,work,1,work+128,16,0,0})==0 && process.last_error==1113);
    m.store(work+256,0x80,8);m.store(work,0xdeadbeef,32);
    CHECK(kernel("MultiByteToWideChar",{65001,8,work+256,1,work,2})==0 && process.last_error==1113 && m.load(work,32)==0xdeadbeef);
    CHECK(kernel("GetCPInfo",{936,work})==1 && m.load(work,32)==2 && m.load(work+6,16)==0xfe81);
    const auto encoded=kernel("EncodePointer",{0xcafebabe});CHECK(kernel("DecodePointer",{encoded})==0xcafebabe);
    const auto slot=kernel("FlsAlloc",{0});CHECK(slot!=0xffffffff && kernel("FlsSetValue",{slot,0x12345678})==1 && kernel("FlsGetValue",{slot})==0x12345678);
    process.state().current_thread_id=77;CHECK(kernel("FlsGetValue",{slot})==0);process.state().current_thread_id=1;CHECK(kernel("FlsGetValue",{slot})==0x12345678 && kernel("FlsFree",{slot})==1);
    CHECK(kernel("GetLocaleInfoW",{0x804,0x20001004,work,2})==2 && m.load(work,32)==936);
    CHECK(kernel("GetLocaleInfoW",{0x409,0x38,work,32})==8 && process.read_wstring(work)==u"January");
    const std::array<unsigned,8> time{2026,10,6,17,14,5,9,0};for(unsigned n=0;n<time.size();++n)m.store(work+512+n*2,time[n],16);
    const auto date_format=process.put_string("yyyy-MM-dd ddd"),time_format=process.put_string("HH:mm:ss");
    CHECK(kernel("GetDateFormatA",{0x409,0,work+512,date_format,work,64})==15 && process.read_string(work)=="2026-10-17 Sat");
    CHECK(kernel("GetTimeFormatA",{0x409,0,work+512,time_format,work,64})==9 && process.read_string(work)=="14:05:09");
    std::ofstream report(argv[1]);report<<"{\"string_page_and_fault_cases\":"<<string_cases<<",\"string_component_comparison\":[";
    const auto legacy_length=[&](U32 at){std::string value;for(unsigned n=0;n<32768;++n){const auto c=m.load(at+n,8);if(!c)return U32(value.size());value+=char(c);}throw std::runtime_error("unterminated fixture");};
    for(unsigned fixture=0;fixture<3;++fixture){const unsigned length=fixture==0?64:fixture==1?256:1024;
        m.copy_in(strings,std::span(letters).first(length));m.store(strings+length,0,8);if(fixture)report<<',';
        report<<"{\"bytes\":"<<length<<",\"calls_per_sample\":64,\"paired_ns\":[";
        for(unsigned pair=0;pair<8;++pair){std::array<std::uint64_t,2> elapsed{};
            for(unsigned order=0;order<2;++order){const auto after=(pair+order)&1;const auto start=std::chrono::steady_clock::now();
                for(unsigned n=0;n<64;++n)CHECK((after?process.string_length(strings):legacy_length(strings))==length);
                elapsed[after]=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();}
            if(pair)report<<',';report<<'['<<elapsed[0]<<','<<elapsed[1]<<']';}
        report<<"]}";
    }
    report<<"],\"schema\":\"wa2.portable-system.v1\",\"profiles_persisted\":true,\"profile_short_buffers\":true,\"profile_struct_checksum\":true,\"format_source_32bit_varargs\":true,\"pe_resource_handles\":true,\"timer_pairing\":true,\"chinese_and_japanese_text\":true,\"utf8_surrogates_and_invalid_input\":true,\"pointer_encoding_and_thread_fls\":true,\"native_windows_apis\":false,\"source_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"passed\":true}\n";
    std::cout<<"PASS portable system: INI, source varargs, PE resource lifetime and timer pairing.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
