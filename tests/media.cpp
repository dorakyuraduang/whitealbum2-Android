#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <msacm.h>
#include <dshow.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include "support.hpp"
#include "wa2_win32.hpp"
#include "native_backend.hpp"
bool code_page_1099(wr::Cpu&,wr::Memory&,std::uint64_t&);
using wr::U32;
#define CHECK(value) do {if(!(value))throw std::runtime_error(#value);}while(false)
static wr::Process* active;
static bool step(wr::Cpu& cpu,wr::Memory& memory,std::uint64_t& budget){if((cpu.eip&~4095u)==0x44b000)return code_page_1099(cpu,memory,budget);if(cpu.eip!=active->entry)return false;cpu.eip=active->resolve("user32.dll","DefWindowProcA");return true;}
int main(int argc,char** argv){try {
    if(argc!=3)throw std::runtime_error("usage: wa2_media_tests original.exe sample.avi|--events-only");
    wr::ProcessOptions options;options.instruction_budget=1000000;options.data_root=std::filesystem::absolute(argv[2]).parent_path();
    wr::Process p(step,options);if(std::string(argv[2])=="--events-only")CHECK(p.memory.enable_linear_memory());active=&p;wr::install_wa2_win32(p);p.load(wr::Image::load(argv[1]));
    auto call=[&](const char* dll,const char* name,std::initializer_list<U32> args){const auto target=p.resolve(dll,name);CHECK(target);return p.callback(target,std::span(args.begin(),args.size()));};
    auto com=[&](U32 object,unsigned slot,std::initializer_list<U32> args){std::vector<U32> values{object};values.insert(values.end(),args.begin(),args.end());return p.callback(p.memory.load(p.memory.load(object,32)+slot*4,32),values);};
    auto ok=[&](U32 result){if(FAILED(HRESULT(result))){std::ostringstream out;out<<"media HRESULT 0x"<<std::hex<<result;throw std::runtime_error(out.str());}};
    const auto buffer=p.allocate_bytes(4096);
    if(std::string(argv[2])=="--events-only"){
        ok(call("ole32.dll","CoInitialize",{0}));
        wr::port::write(p,buffer,CLSID_FilterGraph);wr::port::write(p,buffer+16,IID_IGraphBuilder);
        ok(call("ole32.dll","CoCreateInstance",{buffer,0,CLSCTX_INPROC_SERVER,buffer+16,buffer+64}));
        const auto graph=p.memory.load(buffer+64,32);CHECK(graph);
        for(const auto& iid:{IID_IMediaEvent,IID_IMediaEventEx}){
            wr::port::write(p,buffer+16,iid);ok(com(graph,0,{buffer+16,buffer+64}));
            const auto event=p.memory.load(buffer+64,32);CHECK(event);
            for(auto stale:{1u,13u,14u,0xdeadbeefu})for(auto timeout:{0u,1u}){
                U32 result=0;
                for(unsigned attempt=0;attempt<16;++attempt){
                    p.memory.store(buffer+128,stale,32);p.memory.store(buffer+132,0x12345678,32);p.memory.store(buffer+136,0x87654321,32);
                    result=com(event,8,{buffer+128,buffer+132,buffer+136,timeout});
                    if(FAILED(HRESULT(result)))break;
                    ok(com(event,12,{p.memory.load(buffer+128,32),p.memory.load(buffer+132,32),p.memory.load(buffer+136,32)}));
                }
                CHECK(FAILED(HRESULT(result)));
                CHECK(p.memory.load(buffer+128,32)==0 && p.memory.load(buffer+132,32)==0 && p.memory.load(buffer+136,32)==0);
            }
            const auto fast=p.state().fast_step,block=p.state().block_step;
            CHECK(wr::wa2_native_entry(0x44bd20));
            for(bool native:{false,true}){
                p.state().fast_step=native?fast:nullptr;p.state().block_step=native?block:nullptr;
                p.memory.store(buffer+264,event,32);p.cpu.r[wr::ECX]=buffer+256;
                p.memory.store(p.cpu.r[wr::ESP]-16,1,32);p.memory.store(0xb6f224,1,32);
                CHECK(p.callback(0x44bd20,{})==0);
                CHECK(p.memory.load(0xb6f224,32)==1 && p.memory.load(p.cpu.r[wr::ESP]-16,32)==0);
            }
            p.state().fast_step=fast;p.state().block_step=block;
            com(event,2,{});
        }
        com(graph,2,{});ok(call("ole32.dll","CoUninitialize",{}));
        std::cout<<"PASS empty DirectShow queue: failed HRESULT retained, stale EC_COMPLETE cleared; original/native movie predicate remains playing; no window or screen access\n";
        return 0;
    }
    const auto klass=p.put_string("WA2 media boundary"),title=p.put_string("WA2 hidden video test");
    wr::port::write(p,buffer,std::array<U32,10>{CS_OWNDC,p.entry,0,0,p.image_base,0,0,0,0,klass});CHECK(call("user32.dll","RegisterClassA",{buffer}));
    const auto window=call("user32.dll","CreateWindowExA",{0,klass,title,WS_OVERLAPPEDWINDOW,0,0,96,96,0,0,p.image_base,0});CHECK(window);
    ok(call("ole32.dll","CoInitialize",{0}));
    wr::port::write(p,buffer,CLSID_FilterGraph);wr::port::write(p,buffer+16,IID_IGraphBuilder);
    ok(call("ole32.dll","CoCreateInstance",{buffer,0,CLSCTX_INPROC_SERVER,buffer+16,buffer+64}));const auto graph=p.memory.load(buffer+64,32);CHECK(graph);
    auto query=[&](const GUID& iid){wr::port::write(p,buffer+16,iid);ok(com(graph,0,{buffer+16,buffer+64}));const auto result=p.memory.load(buffer+64,32);CHECK(result);return result;};
    std::u16string name=u"C:\\WinRecomp\\";for(char c:std::filesystem::path(argv[2]).filename().string())name+=char16_t(c);
    const auto filename=p.put_wstring(name);ok(com(graph,13,{filename,0}));
    const auto control=query(IID_IMediaControl),video=query(IID_IVideoWindow),basic=query(IID_IBasicVideo),event=query(IID_IMediaEventEx);
    ok(com(event,7,{buffer+144}));const auto borrowed_event=p.memory.load(buffer+144,32);CHECK(p.state().events.contains(borrowed_event));
    ok(com(video,13,{OAFALSE}));ok(com(video,29,{window}));ok(com(video,9,{WS_CHILD|WS_CLIPSIBLINGS}));ok(com(video,39,{0,0,64,64}));
    HWND native_video=nullptr;
    EnumChildWindows(reinterpret_cast<HWND>(p.gui()->native_window(window)),[](HWND hwnd,LPARAM data)->BOOL{
        char name[128]{};GetClassNameA(hwnd,name,sizeof(name));if(std::string(name)=="VideoRenderer")*reinterpret_cast<HWND*>(data)=hwnd;return TRUE;
    },reinterpret_cast<LPARAM>(&native_video));CHECK(native_video);
    ok(com(video,30,{buffer+128}));CHECK(p.memory.load(buffer+128,32)==window);
    ok(com(event,13,{window,WM_APP+111,0}));ok(com(basic,34,{buffer+128,buffer+132}));CHECK(p.memory.load(buffer+128,32)==64 && p.memory.load(buffer+132,32)==64);
    ok(com(control,8,{}));ok(com(control,10,{2000,buffer+128}));CHECK(p.memory.load(buffer+128,32)==State_Paused);
    p.memory.store(buffer+128,0,32);ok(com(basic,36,{buffer+128,0}));const auto image_size=p.memory.load(buffer+128,32);CHECK(image_size>=40+64*64*3);
    const auto image=p.allocate_bytes(image_size);ok(com(basic,36,{buffer+128,image}));CHECK(p.memory.load(image+4,32)==64);
    const auto header=p.memory.load(image,32),bpp=p.memory.load(image+14,16);CHECK(header>=40 && (bpp==24 || bpp==32));
    const auto pixel=p.memory.load(image+header,32)&0xffffffu;CHECK(pixel==0x112233);
    ok(com(control,7,{}));
    bool completed=false,retained_parameter=false;const auto deadline=::GetTickCount64()+5000;
    while(::GetTickCount64()<deadline && !completed){
        const auto result=com(event,8,{buffer+128,buffer+132,buffer+136,0});
        if(SUCCEEDED(HRESULT(result))){const auto code=p.memory.load(buffer+128,32),first=p.memory.load(buffer+132,32),second=p.memory.load(buffer+136,32);if(!retained_parameter && code!=EC_COMPLETE)retained_parameter=true;else ok(com(event,12,{code,first,second}));completed=code==EC_COMPLETE;}
        MSG message{};while(::PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){::TranslateMessage(&message);::DispatchMessageW(&message);}::Sleep(5);
    }
    CHECK(completed && retained_parameter);ok(com(control,9,{}));
    wr::port::write(p,buffer+16,IID_IGraphBuilder);ok(call("ole32.dll","CoCreateInstance",{buffer,0,CLSCTX_INPROC_SERVER,buffer+16,buffer+160}));const auto other_graph=p.memory.load(buffer+160,32);
    wr::port::write(p,buffer+16,IID_IMediaEventEx);ok(com(other_graph,0,{buffer+16,buffer+164}));const auto other_event=p.memory.load(buffer+164,32);
    ok(com(other_event,7,{buffer+168}));const auto other_borrowed=p.memory.load(buffer+168,32);CHECK(p.state().events.contains(other_borrowed));
    for(auto value:{event,basic,video,control,graph})com(value,2,{});
    const auto retire_deadline=::GetTickCount64()+1000;
    while(IsWindow(native_video) && ::GetTickCount64()<retire_deadline){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))DispatchMessageW(&message);Sleep(5);}
    std::cerr<<"video window after last guest graph release with an outstanding event: "<<(IsWindow(native_video)?"still alive":"destroyed")<<'\n';
    CHECK(!IsWindow(native_video));
    CHECK(!p.state().events.contains(borrowed_event) && p.state().events.contains(other_borrowed));
    com(other_event,2,{});com(other_graph,2,{});CHECK(!p.state().events.contains(other_borrowed));
    std::cout<<"PASS native DirectShow: COM/IDispatch layout, virtual filename, hidden child video, exact DIB pixel, x86 HWND owner, playback completion and FreeEventParams\n";
    const WAVEFORMATEX source_format{WAVE_FORMAT_PCM,1,22050,44100,2,16,0},destination_format{WAVE_FORMAT_PCM,1,22050,22050,1,8,0};
    wr::port::write(p,buffer,source_format);wr::port::write(p,buffer+32,destination_format);
    CHECK(call("msacm32.dll","acmStreamOpen",{buffer+128,0,buffer,buffer+32,0,0,0,ACM_STREAMOPENF_NONREALTIME})==MMSYSERR_NOERROR);const auto stream=p.memory.load(buffer+128,32);CHECK(stream);
    CHECK(call("msacm32.dll","acmStreamSize",{stream,256,buffer+132,ACM_STREAMSIZEF_SOURCE})==MMSYSERR_NOERROR);const auto capacity=p.memory.load(buffer+132,32);CHECK(capacity>=128);
    const auto source=p.allocate_bytes(256),destination=p.allocate_bytes(capacity);for(unsigned n=0;n<128;++n)p.memory.store(source+n*2,16384,16);
    const auto h=buffer+512;wr::port::write(p,h,std::array<U32,21>{84,0,123,source,256,0,456,destination,capacity,0,789,0,0,0,0,0,0,0,0,0,0});
    CHECK(call("msacm32.dll","acmStreamConvert",{stream,h,0})==ACMERR_UNPREPARED);
    CHECK(call("msacm32.dll","acmStreamPrepareHeader",{stream,h,0})==MMSYSERR_NOERROR);
    CHECK(call("msacm32.dll","acmStreamConvert",{stream,h,ACM_STREAMCONVERTF_START|ACM_STREAMCONVERTF_END|ACM_STREAMCONVERTF_BLOCKALIGN})==MMSYSERR_NOERROR);
    CHECK(p.memory.load(h+20,32)==256 && p.memory.load(h+36,32)==128);for(unsigned n=0;n<128;++n)CHECK(p.memory.load(destination+n,8)==192);
    for(unsigned n=11;n<21;++n)CHECK(p.memory.load(h+n*4,32)==0);
    CHECK(call("msacm32.dll","acmStreamUnprepareHeader",{stream,h,0})==MMSYSERR_NOERROR);
    CHECK(call("msacm32.dll","acmStreamClose",{stream,0})==MMSYSERR_NOERROR);
    CHECK(call("msacm32.dll","acmStreamSize",{stream,256,buffer+132,ACM_STREAMSIZEF_SOURCE})==MMSYSERR_INVALHANDLE);
    std::cout<<"PASS native ACM: 16-bit to 8-bit PCM, exact samples and used byte counts, legacy header without native pointers, prepare/unprepare lifetime\n";
    std::cout<<p.report()<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
