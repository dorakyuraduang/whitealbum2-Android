#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winrecomp/process.hpp>
#include <fstream>
#include <iostream>
#include <chrono>
#include <cmath>
#include "wa2_win32.hpp"
#define CHECK(value) do {if(!(value))throw std::runtime_error(#value);}while(false)
static bool no_guest_code(wr::Cpu&,wr::Memory&,std::uint64_t&){throw std::runtime_error("unexpected guest code in API test");}
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::runtime_error("usage: wa2_port_tests original.exe api-coverage.json");
        wr::ProcessOptions options;options.instruction_budget=100000;
        wr::Process p(no_guest_code,options);wr::install_wa2_win32(p);
        const auto image=wr::Image::load(argv[1]);
        std::ofstream inventory(argv[2]);inventory<<"{\"schema\":\"wa2.api-registration.v1\",\"imports\":[";
        bool first=true;unsigned registered=0;
        for(const auto& item:image.imports) {
            const auto name=item.ordinal?"#"+std::to_string(*item.ordinal):item.name;
            const bool found=p.resolve(item.dll,name)!=0;registered+=unsigned(found);
            if(!first)inventory<<',';first=false;
            inventory<<"{\"dll\":"<<wr::quote(item.dll)<<",\"name\":"<<wr::quote(name)<<",\"registered\":"<<(found?"true":"false")<<'}';
        }
        inventory<<"],\"registered\":"<<registered<<",\"total\":"<<image.imports.size()<<",\"registration_is_not_full_api_conformance\":true}\n";
        p.load(image);
        auto call=[&](const char* dll,const char* name,std::initializer_list<wr::U32> args) {
            const auto at=p.resolve(dll,name);CHECK(at);return p.callback(at,std::span(args.begin(),args.size()));
        };
        for(wr::U32 value:{0u,1u,0xffffffffu,0x00400000u}) {
            const auto encoded=call("kernel32.dll","EncodePointer",{value});
            CHECK(call("kernel32.dll","DecodePointer",{encoded})==value);
            if(!value)CHECK(encoded!=0);
        }
        const auto slot=call("kernel32.dll","FlsAlloc",{0});CHECK(slot!=0xffffffffu);
        CHECK(call("kernel32.dll","FlsGetValue",{slot})==0);
        CHECK(call("kernel32.dll","FlsSetValue",{slot,0xdeadbeef})==1);
        CHECK(call("kernel32.dll","FlsGetValue",{slot})==0xdeadbeef);
        CHECK(call("kernel32.dll","FlsFree",{slot})==1);
        CHECK(call("kernel32.dll","FlsGetValue",{slot})==0 && p.last_error==ERROR_INVALID_PARAMETER);
        for(wr::U32 cp:{1252u,932u,936u,65001u,999999u})CHECK(call("kernel32.dll","IsValidCodePage",{cp})==wr::U32(::IsValidCodePage(cp)));
        const auto buffer=p.allocate_bytes(1024),args=p.allocate_bytes(64);
        const auto text=p.put_string("test"),format=p.put_string("%s %08X %d %I64u");
        p.memory.store(args,text,32);p.memory.store(args+4,0xdeadbeef,32);p.memory.store(args+8,wr::U32(-42),32);
        p.memory.store(args+12,0x89abcdef,32);p.memory.store(args+16,0x01234567,32);
        char expected[1024]{};
        const auto expected_count=::wsprintfA(expected,"%s %08X %d %I64u","test",0xdeadbeef,-42,0x0123456789abcdefull);
        CHECK(call("user32.dll","wvsprintfA",{buffer,format,args})==wr::U32(expected_count));
        CHECK(p.read_string(buffer)==expected);
        const auto wide=p.put_wstring(u"wide"),wide_format=p.put_string("%S %ls %c %%");
        p.memory.store(args,wide,32);p.memory.store(args+4,wide,32);p.memory.store(args+8,'!',32);
        ::wsprintfA(expected,"%S %ls %c %%",L"wide",L"wide",'!');
        call("user32.dll","wvsprintfA",{buffer,wide_format,args});CHECK(p.read_string(buffer)==expected);
        const auto cdecl_format=p.put_string("%s/%u");
        const std::array<wr::U32,4> cdecl_args{buffer,cdecl_format,text,12345};
        const auto cdecl_at=p.resolve("user32.dll","wsprintfA");CHECK(cdecl_at);
        p.callback(cdecl_at,cdecl_args,false);CHECK(p.read_string(buffer)=="test/12345");
        const auto unicode=p.put_wstring(u"\u6c49\u5b57A"),precision_format=p.put_string("%3.2S");
        p.memory.store(args,unicode,32);::wsprintfA(expected,"%3.2S",L"\u6c49\u5b57A");
        call("user32.dll","wvsprintfA",{buffer,precision_format,args});CHECK(p.read_string(buffer)==expected);
        auto load64=[&]{return std::uint64_t(p.memory.load(buffer,32))|(std::uint64_t(p.memory.load(buffer+4,32))<<32);};
        CHECK(call("kernel32.dll","QueryPerformanceFrequency",{buffer})==1);const auto frequency=load64();CHECK(frequency>0);
        CHECK(call("kernel32.dll","QueryPerformanceCounter",{buffer})==1);const auto ticks=load64();
        const auto start=std::chrono::steady_clock::now();::Sleep(12);
        CHECK(call("kernel32.dll","QueryPerformanceCounter",{buffer})==1);
        const auto seconds=double(load64()-ticks)/frequency;
        const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        CHECK(seconds>=.012 && std::abs(seconds-elapsed)<.01);
        const auto precise=reinterpret_cast<void(WINAPI*)(LPFILETIME)>(::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"),"GetSystemTimePreciseAsFileTime"));CHECK(precise);
        FILETIME before{},after{};precise(&before);
        call("kernel32.dll","GetSystemTimeAsFileTime",{buffer});precise(&after);
        const auto wall=load64(),wall_before=std::uint64_t(before.dwLowDateTime)|(std::uint64_t(before.dwHighDateTime)<<32),
            wall_after=std::uint64_t(after.dwLowDateTime)|(std::uint64_t(after.dwHighDateTime)<<32);
        CHECK(wall+10>=wall_before && wall<=wall_after+10);
        call("kernel32.dll","InitializeCriticalSectionAndSpinCount",{buffer,4000});
        CHECK(p.memory.load(buffer+4,32)==0xffffffffu && p.memory.load(buffer+20,32)==4000);
        call("kernel32.dll","EnterCriticalSection",{buffer});CHECK(p.memory.load(buffer+8,32)==1);
        call("kernel32.dll","LeaveCriticalSection",{buffer});CHECK(p.memory.load(buffer+8,32)==0);
        CHECK(call("shell32.dll","SHGetMalloc",{0})==wr::U32(E_POINTER));
        CHECK(call("shell32.dll","SHGetMalloc",{buffer})==S_OK);
        const auto allocator=p.memory.load(buffer,32),vtable=p.memory.load(allocator,32);
        auto malloc_call=[&](unsigned method,std::initializer_list<wr::U32> tail) {
            std::vector<wr::U32> values{allocator};values.insert(values.end(),tail.begin(),tail.end());
            return p.callback(p.memory.load(vtable+method*4,32),values);
        };
        auto allocation=malloc_call(3,{32});CHECK(allocation);
        p.memory.store(allocation,0x12345678,32);
        CHECK(malloc_call(7,{allocation})==1 && malloc_call(7,{0})==0xffffffffu);
        CHECK(malloc_call(6,{allocation})>=32 && malloc_call(6,{0})==0xffffffffu);
        CHECK(malloc_call(4,{allocation,0xffffffffu})==0 && p.memory.load(allocation,32)==0x12345678);
        allocation=malloc_call(4,{allocation,8192});CHECK(allocation && p.memory.load(allocation,32)==0x12345678);
        CHECK(malloc_call(4,{allocation,0})==0 && malloc_call(7,{allocation})==0);
        malloc_call(5,{0});
        CHECK(call("shell32.dll","SHGetSpecialFolderLocation",{0,5,buffer})==S_OK);
        const auto pidl=p.memory.load(buffer,32);CHECK(malloc_call(7,{pidl})==1);
        CHECK(call("shell32.dll","SHGetPathFromIDListA",{pidl,buffer+16})==1);
        CHECK(p.read_string(buffer+16)=="C:\\WinRecomp\\Documents");
        malloc_call(5,{pidl});CHECK(call("shell32.dll","SHGetPathFromIDListA",{pidl,buffer+16})==0);
        CHECK(call("shell32.dll","SHGetSpecialFolderLocation",{0,0xff,buffer})==wr::U32(E_INVALIDARG));
        CHECK(p.memory.load(buffer,32)==0);
        const std::array<std::uint8_t,16> unknown{0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
        p.memory.copy_in(args,unknown);CHECK(malloc_call(0,{args,buffer})==S_OK);
        CHECK(p.memory.load(buffer,32)==allocator && malloc_call(2,{})==1);
        p.memory.store(args,0xbad1d,32);CHECK(malloc_call(0,{args,buffer})==wr::U32(E_NOINTERFACE));
        CHECK(p.memory.load(buffer,32)==0 && malloc_call(2,{})==0);
        CHECK(call("ole32.dll","CoInitialize",{0})==S_OK);
        CHECK(call("ole32.dll","CoInitialize",{0})==S_FALSE);
        call("ole32.dll","CoUninitialize",{});call("ole32.dll","CoUninitialize",{});
        CHECK(call("user32.dll","LoadAcceleratorsA",{p.image_base,101})!=0);
        CHECK(call("user32.dll","LoadMenuA",{p.image_base,103})!=0);
        CHECK(call("user32.dll","LoadMenuA",{p.image_base,104})!=0);
        CHECK(call("user32.dll","SetRect",{buffer,1,2,300,400})==1);
        CHECK(p.memory.load(buffer+12,32)==400);
        const auto test_root=std::filesystem::absolute(std::filesystem::path(argv[1]).parent_path()/"build"/("port-api-"+std::to_string(::GetTickCount64())));
        std::filesystem::create_directories(test_root);p.options.data_root=test_root;
        const auto reg_name=p.put_string("Software\\WA2\\PortTest"),value_name=p.put_string("Number");
        CHECK(call("advapi32.dll","RegOpenKeyExA",{0x80000001,reg_name,0,KEY_READ,buffer})==ERROR_FILE_NOT_FOUND);
        CHECK(call("advapi32.dll","RegCreateKeyExA",{0x80000001,reg_name,0,0,0,KEY_ALL_ACCESS,0,buffer,buffer+4})==ERROR_ACCESS_DENIED);
        p.options.allow_file_write=true;
        CHECK(call("advapi32.dll","RegCreateKeyExA",{0x80000001,reg_name,0,0,0,KEY_ALL_ACCESS,0,buffer,buffer+4})==ERROR_SUCCESS);
        CHECK(p.memory.load(buffer+4,32)==REG_CREATED_NEW_KEY);const auto key=p.memory.load(buffer,32);
        p.memory.store(args,123456,32);
        CHECK(call("advapi32.dll","RegSetValueExA",{key,value_name,0,REG_DWORD,args,4})==ERROR_SUCCESS);
        p.memory.store(buffer+8,1,32);
        CHECK(call("advapi32.dll","RegQueryValueExA",{key,value_name,0,buffer+4,buffer+16,buffer+8})==ERROR_MORE_DATA);
        CHECK(p.memory.load(buffer+8,32)==4 && p.memory.load(buffer+4,32)==REG_DWORD);
        CHECK(call("advapi32.dll","RegQueryValueExA",{key,value_name,0,buffer+4,buffer+16,buffer+8})==ERROR_SUCCESS);
        CHECK(p.memory.load(buffer+16,32)==123456);
        CHECK(call("advapi32.dll","RegCloseKey",{key})==ERROR_SUCCESS);
        CHECK(call("advapi32.dll","RegCloseKey",{key})==ERROR_INVALID_HANDLE);
        {
            wr::Process fresh(no_guest_code,p.options);wr::install_wa2_win32(fresh);fresh.load(image);
            auto fresh_call=[&](const char* name,std::initializer_list<wr::U32> values){return fresh.callback(fresh.resolve("advapi32.dll",name),std::span(values.begin(),values.size()));};
            const auto name=fresh.put_string("SOFTWARE\\wa2\\porttest"),value=fresh.put_string("number"),out=fresh.allocate_bytes(32);
            CHECK(fresh_call("RegOpenKeyExA",{0x80000001,name,0,KEY_READ,out})==ERROR_SUCCESS);
            const auto h=fresh.memory.load(out,32);fresh.memory.store(out+4,4,32);
            CHECK(fresh_call("RegQueryValueExA",{h,value,0,0,out+8,out+4})==ERROR_SUCCESS);
            CHECK(fresh.memory.load(out+8,32)==123456);
            CHECK(fresh_call("RegSetValueExA",{h,value,0,REG_DWORD,out+8,4})==ERROR_ACCESS_DENIED);
        }
        const auto ini=p.put_string("port-test.ini"),section=p.put_string("Video"),ini_key=p.put_string("Width"),width=p.put_string("1280");
        CHECK(call("kernel32.dll","WritePrivateProfileStringA",{section,ini_key,width,ini})==1);
        CHECK(call("kernel32.dll","GetPrivateProfileIntA",{section,ini_key,1,ini})==1280);
        CHECK(call("kernel32.dll","GetPrivateProfileStringA",{section,ini_key,0,buffer,32,ini})==4);
        CHECK(p.read_string(buffer)=="1280");
        std::filesystem::create_directory(test_root/"sub");
        const auto sub=p.put_string("sub"),parent_ini=p.put_string("..\\port-test.ini");
        CHECK(call("kernel32.dll","SetCurrentDirectoryA",{sub})==1);
        call("kernel32.dll","GetFullPathNameA",{parent_ini,1024,buffer,buffer+1000});
        CHECK(p.read_string(buffer)=="C:\\WinRecomp\\port-test.ini");
        CHECK(p.read_string(p.memory.load(buffer+1000,32))=="port-test.ini");
        CHECK(call("kernel32.dll","GetPrivateProfileIntA",{section,ini_key,1,parent_ini})==1280);
        bool rejected=false;try{call("user32.dll","wvsprintfA",{0,format,args});}catch(const wr::GuestFault&){rejected=true;}
        CHECK(rejected);
        std::cout<<"PASS target API bridges: varargs, wide strings, pointers, FLS, NLS, clocks, critical sections, Shell IMalloc/PIDL/COM, private registry persistence, INI, virtual cwd\n";
        std::cout<<"Import registrations: "<<registered<<'/'<<image.imports.size()<<" (bounded API profiles)\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
