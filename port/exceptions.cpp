// Guest SEH records contain x86 addresses and x86 CONTEXT layouts. Dispatch to
// statically compiled guest handlers; never unwind native x64 frames as x86.
#include <set>
#include "guest_api.hpp"
#include "exception_abi.hpp"
namespace wr { namespace {
using namespace port;
constexpr U32 Noncontinuable=0xc0000025u,Unwind=0xc0000027u;
static_assert(sizeof(EXCEPTION_RECORD32)==80 && sizeof(WOW64_CONTEXT)==716);
struct ExceptionMemory {Process& p;U32 at;explicit ExceptionMemory(Process& process):p(process),at(p.allocate_bytes(4096)){}~ExceptionMemory(){p.memory.release(at);}};
WOW64_CONTEXT capture(Process& p,U32 arguments){
    const auto& cpu=p.cpu;WOW64_CONTEXT c{};c.ContextFlags=WOW64_CONTEXT_ALL;
    c.Eax=cpu.r[EAX];c.Ecx=cpu.r[ECX];c.Edx=cpu.r[EDX];c.Ebx=cpu.r[EBX];c.Ebp=cpu.r[EBP];c.Esi=cpu.r[ESI];c.Edi=cpu.r[EDI];
    c.Esp=cpu.r[ESP]+4+arguments*4;c.Eip=p.memory.load(cpu.r[ESP],32);c.EFlags=cpu.flags;
    c.SegEs=cpu.selectors[0];c.SegCs=cpu.selectors[1];c.SegSs=cpu.selectors[2];c.SegDs=cpu.selectors[3];c.SegFs=cpu.selectors[4];c.SegGs=cpu.selectors[5];
    std::memcpy(c.ExtendedRegisters,cpu.fp.bytes.data(),512);c.FloatSave.ControlWord=cpu.fp.get16(0);c.FloatSave.StatusWord=cpu.fp.get16(2);c.FloatSave.TagWord=0xffff;
    for(unsigned n=0;n<8;++n)std::memcpy(c.FloatSave.RegisterArea+n*10,cpu.fp.bytes.data()+32+n*16,10);return c;
}
Cpu restore(Process& p,const WOW64_CONTEXT& c){
    Cpu cpu=p.cpu;cpu.r={c.Eax,c.Ecx,c.Edx,c.Ebx,c.Esp,c.Ebp,c.Esi,c.Edi};cpu.eip=c.Eip;cpu.flags=c.EFlags;cpu.defined_flags|=STATUS_FLAGS;
    cpu.selectors={std::uint16_t(c.SegEs),std::uint16_t(c.SegCs),std::uint16_t(c.SegSs),std::uint16_t(c.SegDs),std::uint16_t(c.SegFs),std::uint16_t(c.SegGs)};
    if(c.ContextFlags&0x20)std::memcpy(cpu.fp.bytes.data(),c.ExtendedRegisters,512);
    const auto low=p.memory.load(cpu.fs_base+8,32),high=p.memory.load(cpu.fs_base+4,32);if(c.Esp<low || c.Esp>high)throw GuestFault(FaultKind::memory,c.Esp,"exception continuation outside guest stack");return cpu;
}
U32 next_frame(Process& p,U32 frame,std::set<U32>& visited){
    if(visited.size()>=256 || !visited.insert(frame).second || frame%4)throw GuestFault(FaultKind::memory,frame,"invalid guest SEH frame chain");
    const auto low=p.memory.load(p.cpu.fs_base+8,32),high=p.memory.load(p.cpu.fs_base+4,32);
    if(frame<low || std::uint64_t(frame)+8>high)throw GuestFault(FaultKind::memory,frame,"guest SEH frame outside thread stack");return p.memory.load(frame,32);
}
U32 filter(Process& p,U32 pointers){
    p.memory.check(pointers,8,Memory::Read);const auto record=p.memory.load(pointers,32),context=p.memory.load(pointers+4,32);
    p.memory.check(record,sizeof(EXCEPTION_RECORD32),Memory::Read);p.memory.check(context,sizeof(WOW64_CONTEXT),Memory::Read|Memory::Write);
    if(!p.state().unhandled_filter)return EXCEPTION_EXECUTE_HANDLER;
    const std::array<U32,1> args{pointers};return p.callback(p.state().unhandled_filter,args);
}
}
void install_wa2_exceptions(Process& p){
    api(p,"kernel32.dll","UnhandledExceptionFilter",1,[](Process& q,Args a){return filter(q,a[0]);});
    api(p,"kernel32.dll","RaiseException",4,[](Process& q,Args a)->U32 {
        ExceptionMemory storage(q);const auto record=storage.at,context=record+128,pointers=record+1024,dispatcher=record+1040;
        EXCEPTION_RECORD32 exception{};exception.ExceptionCode=a[0];exception.ExceptionFlags=a[1]&EXCEPTION_NONCONTINUABLE;exception.ExceptionAddress=q.cpu.eip;
        if(a[3]){exception.NumberParameters=std::min<U32>(a[2],EXCEPTION_MAXIMUM_PARAMETERS);for(U32 n=0;n<exception.NumberParameters;++n)exception.ExceptionInformation[n]=q.memory.load(a[3]+n*4,32);}
        write(q,record,exception);write(q,context,capture(q,4));q.memory.store(pointers,record,32);q.memory.store(pointers+4,context,32);
        auto frame=q.memory.load(q.cpu.fs_base,32);std::set<U32> visited;
        while(frame!=0xffffffffu){const auto next=next_frame(q,frame,visited),handler=q.memory.load(frame+4,32);q.memory.check(handler,1,Memory::Execute);
            const std::array<U32,4> args{record,frame,context,dispatcher};const auto disposition=q.callback(handler,args,false);
            if(disposition==0){if(exception.ExceptionFlags&EXCEPTION_NONCONTINUABLE){q.exit(Noncontinuable);return 0;}q.state().api_transfer=restore(q,read<WOW64_CONTEXT>(q,context));return 0;}
            if(disposition!=1 && disposition!=2)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"invalid guest exception-dispatch disposition");frame=next;
        }
        const auto disposition=filter(q,pointers);
        if(disposition==U32(EXCEPTION_CONTINUE_EXECUTION) && !(exception.ExceptionFlags&EXCEPTION_NONCONTINUABLE))q.state().api_transfer=restore(q,read<WOW64_CONTEXT>(q,context));
        else q.exit(disposition==U32(EXCEPTION_CONTINUE_EXECUTION)?Noncontinuable:exception.ExceptionCode);return 0;
    });
    api(p,"kernel32.dll","RtlUnwind",4,[](Process& q,Args a)->U32 {
        ExceptionMemory storage(q);const auto context=storage.at+128,dispatcher=storage.at+1040;
        EXCEPTION_RECORD32 exception{};if(a[2])exception=read<EXCEPTION_RECORD32>(q,a[2]);else{exception.ExceptionCode=Unwind;exception.ExceptionAddress=q.cpu.eip;}
        exception.ExceptionFlags|=EXCEPTION_UNWINDING;if(!a[0])exception.ExceptionFlags|=EXCEPTION_EXIT_UNWIND;write(q,storage.at,exception);
        auto captured=capture(q,4);captured.Eax=a[3];write(q,context,captured);
        auto frame=q.memory.load(q.cpu.fs_base,32);std::set<U32> visited;
        while(frame!=0xffffffffu && frame!=a[0]){const auto next=next_frame(q,frame,visited),handler=q.memory.load(frame+4,32);q.memory.check(handler,1,Memory::Execute);
            const std::array<U32,4> args{storage.at,frame,context,dispatcher};const auto disposition=q.callback(handler,args,false);
            if(disposition!=1)throw GuestFault(FaultKind::unsupported,q.cpu.eip,"collided guest unwind needs an additional dispatcher context");q.memory.store(q.cpu.fs_base,next,32);frame=next;
        }
        if(a[0] && frame!=a[0])throw GuestFault(FaultKind::memory,a[0],"unwind target is not in the guest SEH chain");
        if(a[0] && a[1]){captured.Eip=a[1];q.state().api_transfer=restore(q,captured);}return a[3];
    });
}
}
