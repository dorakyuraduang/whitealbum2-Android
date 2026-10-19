// Synchronous ACM streams retain native headers and owned source/destination
// staging buffers between Prepare/Convert/Unprepare. No driver pointer is copied
// into the legacy 84-byte guest ACMSTREAMHEADER.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <msacm.h>
#include <map>
#include <memory>
#include "support.hpp"
#include "wa2_win32.hpp"
namespace wr {
namespace {
using namespace port;
std::vector<std::uint8_t> format(Process& p,U32 at){const auto extra=p.memory.load(at+16,16);if(extra>4096)throw GuestFault(FaultKind::unsupported,at,"ACM format extension size limit");std::vector<std::uint8_t> data(18+extra);p.memory.copy_out(at,data);return data;}
struct Header {
    ACMSTREAMHEADER native{};
    U32 source{},destination{};
    std::vector<std::uint8_t> input,output;
};
struct Stream {
    HACMSTREAM native{};
    std::map<U32,std::unique_ptr<Header>> headers;
    ~Stream(){if(native){::acmStreamReset(native,0);for(auto& [at,h]:headers)::acmStreamUnprepareHeader(native,&h->native,0);::acmStreamClose(native,0);}}
};
struct Acm {
    Process& p;
    std::map<U32,std::unique_ptr<Stream>> streams;
    std::uint64_t opened{},converted{},bytes_out{};
    explicit Acm(Process& process):p(process){}
    void write_header(U32 at,const Header& h){p.memory.store(at+4,h.native.fdwStatus,32);p.memory.store(at+20,h.native.cbSrcLengthUsed,32);p.memory.store(at+36,h.native.cbDstLengthUsed,32);}
    U32 prepare(Args a){
        auto stream=streams.find(a[0]);if(stream==streams.end())return MMSYSERR_INVALHANDLE;
        p.memory.check(a[1],84,Memory::Read|Memory::Write);const auto fields=read<std::array<U32,21>>(p,a[1]);if(fields[0]<84 || fields[4]>16u*1024*1024 || fields[8]>16u*1024*1024)return MMSYSERR_INVALPARAM;
        if(stream->second->headers.contains(a[1]))return MMSYSERR_NOERROR;
        auto h=std::make_unique<Header>();h->source=fields[3];h->destination=fields[7];h->input.resize(fields[4]);h->output.resize(fields[8]);
        p.memory.check(h->source,h->input.size(),Memory::Read);p.memory.check(h->destination,h->output.size(),Memory::Write);
        h->native.cbStruct=sizeof(ACMSTREAMHEADER);h->native.fdwStatus=fields[1];h->native.dwUser=fields[2];h->native.pbSrc=h->input.data();h->native.cbSrcLength=fields[4];h->native.dwSrcUser=fields[6];h->native.pbDst=h->output.data();h->native.cbDstLength=fields[8];h->native.dwDstUser=fields[10];
        const auto result=::acmStreamPrepareHeader(stream->second->native,&h->native,a[2]);if(result==MMSYSERR_NOERROR){write_header(a[1],*h);stream->second->headers.emplace(a[1],std::move(h));}return result;
    }
    U32 convert(Args a){
        auto stream=streams.find(a[0]);if(stream==streams.end())return MMSYSERR_INVALHANDLE;auto header=stream->second->headers.find(a[1]);if(header==stream->second->headers.end())return ACMERR_UNPREPARED;
        p.memory.check(a[1],84,Memory::Read|Memory::Write);const auto fields=read<std::array<U32,21>>(p,a[1]);auto& h=*header->second;
        if(fields[0]<84 || fields[3]!=h.source || fields[7]!=h.destination || fields[4]>h.input.size() || fields[8]>h.output.size())return MMSYSERR_INVALPARAM;
        p.memory.copy_out(h.source,std::span(h.input).first(fields[4]));p.memory.check(h.destination,fields[8],Memory::Write);
        h.native.cbSrcLength=fields[4];h.native.cbDstLength=fields[8];h.native.cbSrcLengthUsed=0;h.native.cbDstLengthUsed=0;h.native.dwUser=fields[2];h.native.dwSrcUser=fields[6];h.native.dwDstUser=fields[10];
        const auto result=::acmStreamConvert(stream->second->native,&h.native,a[2]);write_header(a[1],h);
        if(result==MMSYSERR_NOERROR){if(h.native.cbDstLengthUsed>fields[8] || h.native.cbSrcLengthUsed>fields[4])throw std::runtime_error("ACM driver returned invalid used lengths");p.memory.copy_in(h.destination,std::span(h.output).first(h.native.cbDstLengthUsed));++converted;bytes_out+=h.native.cbDstLengthUsed;}return result;
    }
    void install(){
        api(p,"msacm32.dll","acmFormatSuggest",5,[this](Process&,Args a)->U32 {
            if(a[0])return MMSYSERR_INVALHANDLE;if(a[3]<18 || a[3]>4114)return MMSYSERR_INVALPARAM;auto source=format(p,a[1]);std::vector<std::uint8_t> destination(a[3]);p.memory.copy_out(a[2],destination);p.memory.check(a[2],a[3],Memory::Write);
            const auto result=::acmFormatSuggest(nullptr,reinterpret_cast<WAVEFORMATEX*>(source.data()),reinterpret_cast<WAVEFORMATEX*>(destination.data()),a[3],a[4]);if(result==MMSYSERR_NOERROR)p.memory.copy_in(a[2],destination);return result;
        });
        api(p,"msacm32.dll","acmStreamOpen",8,[this](Process&,Args a)->U32 {
            if(a[1])return MMSYSERR_INVALHANDLE;if(a[4] || a[5] || (a[7]&ACM_STREAMOPENF_ASYNC))throw GuestFault(FaultKind::unsupported,p.cpu.eip,"ACM filter/asynchronous callback requires a separate wrapper");
            if(a[0]){p.memory.check(a[0],4,Memory::Write);p.memory.store(a[0],0,32);}auto source=format(p,a[2]),destination=format(p,a[3]);
            HACMSTREAM handle=nullptr;const auto result=::acmStreamOpen(a[0]?&handle:nullptr,nullptr,reinterpret_cast<WAVEFORMATEX*>(source.data()),reinterpret_cast<WAVEFORMATEX*>(destination.data()),nullptr,0,a[6],a[7]);
            if(result==MMSYSERR_NOERROR && handle){auto stream=std::make_unique<Stream>();stream->native=handle;const auto token=p.state().next_handle;p.state().next_handle+=16;streams.emplace(token,std::move(stream));p.memory.store(a[0],token,32);++opened;}return result;
        });
        api(p,"msacm32.dll","acmStreamSize",4,[this](Process&,Args a)->U32 {auto it=streams.find(a[0]);if(it==streams.end())return MMSYSERR_INVALHANDLE;p.memory.check(a[2],4,Memory::Write);DWORD bytes=0;const auto result=::acmStreamSize(it->second->native,a[1],&bytes,a[3]);if(result==MMSYSERR_NOERROR)p.memory.store(a[2],bytes,32);return result;});
        api(p,"msacm32.dll","acmStreamPrepareHeader",3,[this](Process&,Args a){return prepare(a);});
        api(p,"msacm32.dll","acmStreamConvert",3,[this](Process&,Args a){return convert(a);});
        api(p,"msacm32.dll","acmStreamUnprepareHeader",3,[this](Process&,Args a)->U32 {
            auto stream=streams.find(a[0]);if(stream==streams.end())return MMSYSERR_INVALHANDLE;auto header=stream->second->headers.find(a[1]);if(header==stream->second->headers.end())return MMSYSERR_NOERROR;
            p.memory.check(a[1],84,Memory::Read|Memory::Write);const auto result=::acmStreamUnprepareHeader(stream->second->native,&header->second->native,a[2]);if(result==MMSYSERR_NOERROR){write_header(a[1],*header->second);stream->second->headers.erase(header);}return result;
        });
        api(p,"msacm32.dll","acmStreamClose",2,[this](Process&,Args a)->U32 {auto it=streams.find(a[0]);if(it==streams.end())return MMSYSERR_INVALHANDLE;const auto result=::acmStreamClose(it->second->native,a[1]);if(result==MMSYSERR_NOERROR){it->second->native=nullptr;streams.erase(it);}return result;});
    }
    std::string report() const {return "{\"backend\":\"native-acm\",\"streams_opened\":"+std::to_string(opened)+",\"conversions\":"+std::to_string(converted)+",\"output_bytes\":"+std::to_string(bytes_out)+"}";}
};
}
void install_wa2_acm(Process& p){auto state=std::make_shared<Acm>(p);state->install();p.state().shutdown.push_back([state]{state->streams.clear();});p.state().reports["acm"]=[state]{return state->report();};}
}
