// DirectShow uses the native x64 graph and codecs. Guest COM objects contain
// dispatch tokens; HWNDs, event handles, LONG_PTR values and BSTRs are marshalled.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dshow.h>
#include <oleauto.h>
#include <algorithm>
#include <bit>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <utility>
#include "support.hpp"
#include "wa2_win32.hpp"
namespace wr {
namespace {
using namespace port;
enum class Kind {unknown,graph,filter_graph,control,event,event_ex,audio,video_window,basic_video,seeking,position};
#include "media_tables.inc"
LONG integer(U32 value){return std::bit_cast<std::int32_t>(value);}
double real(Args a,unsigned index){return std::bit_cast<double>(std::uint64_t(a[index])|(std::uint64_t(a[index+1])<<32));}
struct Owner {IUnknown* value{};~Owner(){if(value)value->Release();}IUnknown* keep(){return std::exchange(value,nullptr);}};
struct String {BSTR value{};~String(){if(value)::SysFreeString(value);}};
struct GraphLifetime {std::uint64_t guest_references{};};
struct Object {Kind kind;IUnknown* native;U32 refs,owner{},drain{};std::shared_ptr<GraphLifetime> lifetime;};
class Media {
    Process& p;
    std::map<U32,Object> objects;
    std::map<Kind,U32> vtables;
    std::map<U32,U32> bstrs;
    struct Event {IMediaEvent* source;LONG code;LONG_PTR first,second;U32 guest_first,guest_second;std::shared_ptr<GraphLifetime> lifetime;};
    std::vector<Event> events;
    std::map<U32,std::shared_ptr<GraphLifetime>> borrowed_events;
    U32 next_opaque{0xbd000000};
    bool stopped{};
    std::uint64_t graphs{},rendered{},runs{},delivered{};
    std::uint64_t pauses{},stops{};std::vector<LONG> event_codes,visibility_changes;
    std::vector<U32> event_return_pcs,stop_return_pcs,stop_parent_pcs,stop_reason_pcs;
    std::string call_sites_report() const {
        std::ostringstream out;out<<"{\"events\":[";bool first=true;
        for(auto pc:event_return_pcs){if(!first)out<<',';first=false;out<<pc;}
        out<<"],\"stops\":[";first=true;
        for(auto pc:stop_return_pcs){if(!first)out<<',';first=false;out<<pc;}
        out<<"],\"stop_parents\":[";first=true;
        for(auto pc:stop_parent_pcs){if(!first)out<<',';first=false;out<<pc;}
        out<<"],\"stop_reasons\":[";first=true;
        for(auto pc:stop_reason_pcs){if(!first)out<<',';first=false;out<<pc;}
        out<<"]}";return out.str();
    }
    HRESULT last_render{};
    std::string last_file;
    [[noreturn]]void stop(const std::string& message){throw GuestFault(FaultKind::unsupported,p.cpu.eip,"DirectShow: "+message);}
    Object& object(U32 at){auto it=objects.find(at);if(it==objects.end() || !it->second.native || !it->second.refs)stop("stale guest COM object");return it->second;}
    static std::optional<Kind> kind_for_iid(REFIID iid){
        if(IsEqualIID(iid,IID_IUnknown))return Kind::unknown;
#define IID_KIND(name,kind) if(IsEqualIID(iid,IID_##name))return Kind::kind
        IID_KIND(IGraphBuilder,graph);IID_KIND(IFilterGraph,filter_graph);IID_KIND(IMediaControl,control);IID_KIND(IMediaEvent,event);IID_KIND(IMediaEventEx,event_ex);IID_KIND(IBasicAudio,audio);IID_KIND(IVideoWindow,video_window);IID_KIND(IBasicVideo,basic_video);IID_KIND(IMediaSeeking,seeking);IID_KIND(IMediaPosition,position);
#undef IID_KIND
        return {};
    }
    U32 wrap(IUnknown* value,Kind kind,std::shared_ptr<GraphLifetime> lifetime={}){Owner owner{value};if(!value)return 0;for(auto& [at,o]:objects)if(o.native==value && o.kind==kind){++o.refs;++o.lifetime->guest_references;owner.keep();return at;}if(objects.size()>=256)stop("media COM object limit");if(!lifetime)lifetime=std::make_shared<GraphLifetime>();const auto table=vtable(kind),at=p.allocate_bytes(4);p.memory.store(at,table,32);p.memory.protect(at,4096,Memory::Read);objects.emplace(at,Object{kind,value,1,0,0,lifetime});++lifetime->guest_references;owner.keep();return at;}
    void retire_graph(const std::shared_ptr<GraphLifetime>& lifetime){
        // Native event-parameter and borrowed-handle pins must not keep a
        // stopped VideoRenderer alive after the guest released its last COM
        // interface. Retire only this graph; other graphs remain independent.
        for(std::size_t n=0;n<events.size();)if(events[n].lifetime==lifetime){auto event=events[n];events.erase(events.begin()+n);event.source->FreeEventParams(event.code,event.first,event.second);event.source->Release();}else ++n;
        for(auto it=borrowed_events.begin();it!=borrowed_events.end();)if(it->second==lifetime){const auto token=it->first;it=borrowed_events.erase(it);p.state().events.erase(token);}else ++it;
    }
    U32 vtable(Kind kind){if(auto it=vtables.find(kind);it!=vtables.end())return it->second;const auto info=methods(kind);const auto at=p.allocate_bytes(info.size()*4);for(unsigned n=0;n<info.size();++n){const auto name="COM."+std::to_string(unsigned(kind))+"."+info[n].name;api(p,"wa2.media",name,info[n].arguments,[this,kind,n](Process&,Args a){auto& o=object(a[0]);if(o.kind!=kind)stop("wrong media vtable");return method(o,n,a);});p.memory.store(at+n*4,p.resolve("wa2.media",name),32);}p.memory.protect(at,4096,Memory::Read);vtables.emplace(kind,at);return at;}
    template<class T,class F>U32 output(U32 at,F fn){p.memory.check(at,sizeof(T),Memory::Write);T value{};const auto hr=fn(&value);if(SUCCEEDED(hr))write(p,at,value);return U32(hr);}
    template<class F>U32 four(Args a,F fn){for(unsigned n=1;n<=4;++n)p.memory.check(a[n],4,Memory::Write);std::array<LONG,4> values{};const auto hr=fn(values.data());if(SUCCEEDED(hr))for(unsigned n=0;n<4;++n)p.memory.store(a[n+1],U32(values[n]),32);return U32(hr);}
    template<class F>U32 two(Args a,F fn){p.memory.check(a[1],4,Memory::Write);p.memory.check(a[2],4,Memory::Write);LONG x=0,y=0;const auto hr=fn(&x,&y);if(SUCCEEDED(hr)){p.memory.store(a[1],U32(x),32);p.memory.store(a[2],U32(y),32);}return U32(hr);}
    OAHWND window(U32 at){return OAHWND(p.gui()->native_window(at));}
    std::filesystem::path file(U32 at){const auto wide=p.read_wstring(at);std::string name;for(auto ch:wide){if(ch>127)stop("non-ASCII virtual media filename");name+=char(ch);}return guest_file_path(p,name);}
    U32 bstr(std::u16string_view text){if(text.size()>1048576)stop("BSTR size limit");const auto base=p.allocate_bytes(4+(text.size()+1)*2),at=base+4;p.memory.store(base,U32(text.size()*2),32);for(std::size_t n=0;n<text.size();++n)p.memory.store(at+U32(n*2),text[n],16);p.memory.store(at+U32(text.size()*2),0,16);bstrs.emplace(at,base);return at;}
    String string(U32 at){const auto text=p.read_wstring(at);auto value=::SysAllocStringLen(reinterpret_cast<const OLECHAR*>(text.data()),UINT(text.size()));if(!value)throw std::bad_alloc();return String{value};}
    U32 scalar_or_token(LONG_PTR value){const auto bits=std::uintptr_t(value);if(bits<=0xffffffffull || bits>=0xffffffff80000000ull)return U32(bits);const auto token=next_opaque;next_opaque+=16;return token;}
    U32 query(Object& o,Args a){const auto iid=read<GUID>(p,a[1]);p.memory.check(a[2],4,Memory::Write);p.memory.store(a[2],0,32);const auto kind=kind_for_iid(iid);void* value=nullptr;const auto hr=o.native->QueryInterface(iid,&value);Owner owner{static_cast<IUnknown*>(value)};if(SUCCEEDED(hr)){if(!kind)stop("unmodeled media QueryInterface");p.memory.store(a[2],wrap(owner.keep(),*kind,o.lifetime),32);}return U32(hr);}
    U32 method(Object&,unsigned,Args);
public:
    explicit Media(Process& process):p(process){}
    ~Media(){shutdown();}
    void shutdown() noexcept {
        if(stopped)return;stopped=true;
        for(auto& [at,o]:objects)if(o.native && o.kind==Kind::control)static_cast<IMediaControl*>(o.native)->Stop();
        for(auto& [at,o]:objects)if(o.native && o.kind==Kind::video_window){auto* v=static_cast<IVideoWindow*>(o.native);v->put_Visible(OAFALSE);v->put_MessageDrain(0);v->put_Owner(0);}
        for(auto& e:events){e.source->FreeEventParams(e.code,e.first,e.second);e.source->Release();}events.clear();
        for(const auto& [token,lifetime]:borrowed_events)p.state().events.erase(token);borrowed_events.clear();
        for(auto& [at,o]:objects)if(o.native){for(U32 n=0;n<o.refs;++n)o.native->Release();o.native=nullptr;}
    }
    std::string report() const {std::ostringstream out;out<<"{\"backend\":\"native-directshow\",\"graphs_created\":"<<graphs<<",\"files_rendered\":"<<rendered<<",\"runs\":"<<runs<<",\"events_delivered\":"<<delivered<<",\"last_render_hresult\":"<<U32(last_render)<<",\"last_file\":"<<quote(last_file)<<",\"pauses\":"<<pauses<<",\"stops\":"<<stops<<",\"live_guest_interfaces\":"<<objects.size()<<",\"pending_event_params\":"<<events.size()<<",\"borrowed_event_handles\":"<<borrowed_events.size()<<",\"event_codes\":[";bool first=true;for(auto code:event_codes){if(!first)out<<',';first=false;out<<code;}out<<"],\"visibility_changes\":[";first=true;for(auto visible:visibility_changes){if(!first)out<<',';first=false;out<<visible;}out<<"],\"call_sites\":"<<call_sites_report()<<'}';return out.str();}
    void install(){
        api(p,"ole32.dll","CoCreateInstance",5,[this](Process&,Args a)->U32 {
            const auto clsid=read<GUID>(p,a[0]),iid=read<GUID>(p,a[3]);if(!a[4])return U32(E_POINTER);p.memory.check(a[4],4,Memory::Write);p.memory.store(a[4],0,32);if(a[1])return U32(CLASS_E_NOAGGREGATION);
            if(!IsEqualGUID(clsid,CLSID_FilterGraph) && !IsEqualGUID(clsid,CLSID_FilterGraphNoThread))stop("CoCreateInstance class outside the DirectShow graph profile");
            const auto kind=kind_for_iid(iid);void* value=nullptr;const auto hr=::CoCreateInstance(clsid,nullptr,a[2],iid,&value);Owner owner{static_cast<IUnknown*>(value)};
            if(SUCCEEDED(hr)){if(!kind)stop("CoCreateInstance interface needs a media wrapper");p.memory.store(a[4],wrap(owner.keep(),*kind),32);++graphs;}return U32(hr);
        });
        api(p,"oleaut32.dll","SysAllocString",1,[this](Process&,Args a)->U32 {return a[0]?bstr(p.read_wstring(a[0])):0;});
        api(p,"oleaut32.dll","SysAllocStringLen",2,[this](Process&,Args a)->U32 {if(a[1]>1048576)return 0;std::u16string text(a[1],u'\0');if(a[0])for(U32 n=0;n<a[1];++n)text[n]=char16_t(p.memory.load(a[0]+n*2,16));return bstr(text);});
        api(p,"oleaut32.dll","SysStringLen",1,[this](Process&,Args a)->U32 {return a[0]?p.memory.load(a[0]-4,32)/2:0;});
        api(p,"oleaut32.dll","SysFreeString",1,[this](Process&,Args a)->U32 {if(!a[0])return 0;auto it=bstrs.find(a[0]);if(it==bstrs.end())stop("foreign guest BSTR");p.memory.release(it->second);bstrs.erase(it);return 0;});
    }
};
U32 Media::method(Object& o,unsigned slot,Args a){
    if(slot==0)return query(o,a);if(slot==1){++o.refs;++o.lifetime->guest_references;return o.native->AddRef();}if(slot==2){const auto lifetime=o.lifetime;--o.refs;if(!--lifetime->guest_references)retire_graph(lifetime);const auto count=o.native->Release();if(!o.refs){p.memory.release(a[0]);objects.erase(a[0]);}return count;}
    if(slot==3 && o.kind!=Kind::graph && o.kind!=Kind::filter_graph && o.kind!=Kind::seeking && o.kind!=Kind::unknown)return output<UINT>(a[1],[&](auto* out){return static_cast<IDispatch*>(o.native)->GetTypeInfoCount(out);});
    if(o.kind==Kind::graph || o.kind==Kind::filter_graph){auto* graph=static_cast<IGraphBuilder*>(o.native);switch(slot){
        case 10:return U32(graph->SetDefaultSyncSource());
        case 13:{const auto path=file(a[1]);const auto playlist=a[2]?p.read_wstring(a[2]):std::u16string();last_file=path.string();last_render=graph->RenderFile(path.c_str(),a[2]?reinterpret_cast<const wchar_t*>(playlist.c_str()):nullptr);if(SUCCEEDED(last_render))++rendered;return U32(last_render);}
        case 16:return U32(graph->Abort());case 17:return U32(graph->ShouldOperationContinue());
    }}
    if(o.kind==Kind::control){auto* control=static_cast<IMediaControl*>(o.native);switch(slot){
        case 7:{const auto hr=control->Run();if(SUCCEEDED(hr))++runs;return U32(hr);}case 8:{const auto hr=control->Pause();if(SUCCEEDED(hr))++pauses;return U32(hr);}case 9:{if(stop_return_pcs.size()<128){const auto site=p.recent_transfers.empty()?0:p.recent_transfers.back().return_address;stop_return_pcs.push_back(site);U32 parent=0;if(site==0x44bb41 && p.cpu.r[ESP]<=0xffffffefu)try{parent=p.memory.load(p.cpu.r[ESP]+16,32);}catch(const GuestFault&){}stop_parent_pcs.push_back(parent);
            // The audited 0x430d40 destructor saves ESI before calling
            // 0x44bb10. Its caller distinguishes completion from the guest's
            // own skip branch; collect only this code address, never input.
            U32 reason=0;if(parent==0x430d7b && p.cpu.r[ESP]<=0xffffffe7u)try{reason=p.memory.load(p.cpu.r[ESP]+24,32);}catch(const GuestFault&){}
            stop_reason_pcs.push_back(reason);}const auto hr=control->Stop();if(SUCCEEDED(hr))++stops;return U32(hr);}
        case 10:return output<OAFilterState>(a[2],[&](auto* out){return control->GetState(integer(a[1]),out);});
        case 11:{const auto path=file(a[1]);String name{::SysAllocStringLen(path.c_str(),UINT(path.native().size()))};if(!name.value)return U32(E_OUTOFMEMORY);last_file=path.string();last_render=control->RenderFile(name.value);if(SUCCEEDED(last_render))++rendered;return U32(last_render);}
        case 15:return U32(control->StopWhenReady());
    }}
    if(o.kind==Kind::event || o.kind==Kind::event_ex){auto* event=static_cast<IMediaEvent*>(o.native);switch(slot){
        case 7:{p.memory.check(a[1],4,Memory::Write);OAEVENT value=0;const auto hr=event->GetEventHandle(&value);if(SUCCEEDED(hr)){const auto token=p.state().next_handle;p.state().next_handle+=16;auto handle=std::make_shared<Win32State::NativeEvent>();handle->native=std::uintptr_t(value);o.native->AddRef();handle->close=[value=o.native]{value->Release();};p.state().events.emplace(token,std::move(handle));borrowed_events.emplace(token,o.lifetime);p.memory.store(a[1],token,32);}return U32(hr);}
        // WA2 at 0x44bd20 ignores HRESULT and compares its stack event code.
        // An empty queue must not leave an old EC_COMPLETE in that slot. Keep
        // the failed HRESULT while returning deterministic zeroed outputs.
        case 8:{for(unsigned n=1;n<=3;++n)p.memory.check(a[n],4,Memory::Write);LONG code=0;LONG_PTR first=0,second=0;const auto site=p.recent_transfers.empty()?0:p.recent_transfers.back().return_address;const auto hr=event->GetEvent(&code,&first,&second,integer(a[4]));if(FAILED(hr)){for(unsigned n=1;n<=3;++n)p.memory.store(a[n],0,32);return U32(hr);}if(SUCCEEDED(hr)){const auto guest_first=scalar_or_token(first),guest_second=scalar_or_token(second);event->AddRef();events.push_back({event,code,first,second,guest_first,guest_second,o.lifetime});p.memory.store(a[1],U32(code),32);p.memory.store(a[2],guest_first,32);p.memory.store(a[3],guest_second,32);++delivered;if(event_codes.size()<128){event_codes.push_back(code);event_return_pcs.push_back(site);}}return U32(hr);}
        case 9:return output<LONG>(a[2],[&](auto* out){return event->WaitForCompletion(integer(a[1]),out);});
        case 10:return U32(event->CancelDefaultHandling(integer(a[1])));case 11:return U32(event->RestoreDefaultHandling(integer(a[1])));
        case 12:{const auto it=std::find_if(events.begin(),events.end(),[&](const auto& value){return value.source==event && value.code==integer(a[1]) && value.guest_first==a[2] && value.guest_second==a[3];});if(it==events.end())return U32(E_INVALIDARG);const auto hr=event->FreeEventParams(it->code,it->first,it->second);if(SUCCEEDED(hr)){it->source->Release();events.erase(it);}return U32(hr);}
        case 13:if(o.kind==Kind::event_ex)return U32(static_cast<IMediaEventEx*>(o.native)->SetNotifyWindow(window(a[1]),integer(a[2]),LONG_PTR(integer(a[3]))));break;
        case 14:if(o.kind==Kind::event_ex)return U32(static_cast<IMediaEventEx*>(o.native)->SetNotifyFlags(integer(a[1])));break;
        case 15:if(o.kind==Kind::event_ex)return output<LONG>(a[1],[&](auto* out){return static_cast<IMediaEventEx*>(o.native)->GetNotifyFlags(out);});break;
    }}
    if(o.kind==Kind::audio){auto* audio=static_cast<IBasicAudio*>(o.native);switch(slot){case 7:return U32(audio->put_Volume(integer(a[1])));case 8:return output<LONG>(a[1],[&](auto* out){return audio->get_Volume(out);});case 9:return U32(audio->put_Balance(integer(a[1])));case 10:return output<LONG>(a[1],[&](auto* out){return audio->get_Balance(out);});}}
    if(o.kind==Kind::video_window){auto* video=static_cast<IVideoWindow*>(o.native);switch(slot){
        case 7:{auto text=string(a[1]);return U32(video->put_Caption(text.value));}
        case 8:{p.memory.check(a[1],4,Memory::Write);String text;const auto hr=video->get_Caption(&text.value);if(SUCCEEDED(hr))p.memory.store(a[1],bstr(std::u16string_view(reinterpret_cast<const char16_t*>(text.value),::SysStringLen(text.value))),32);return U32(hr);}
        case 9:return U32(video->put_WindowStyle(integer(a[1])));case 10:return output<LONG>(a[1],[&](auto* out){return video->get_WindowStyle(out);});
        case 11:return U32(video->put_WindowStyleEx(integer(a[1])));case 12:return output<LONG>(a[1],[&](auto* out){return video->get_WindowStyleEx(out);});
        case 13:return U32(video->put_AutoShow(integer(a[1])));case 14:return output<LONG>(a[1],[&](auto* out){return video->get_AutoShow(out);});
        case 15:return U32(video->put_WindowState(integer(a[1])));case 16:return output<LONG>(a[1],[&](auto* out){return video->get_WindowState(out);});
        case 17:return U32(video->put_BackgroundPalette(integer(a[1])));case 18:return output<LONG>(a[1],[&](auto* out){return video->get_BackgroundPalette(out);});
        case 19:{const auto hr=video->put_Visible(integer(a[1]));if(SUCCEEDED(hr) && visibility_changes.size()<128)visibility_changes.push_back(integer(a[1]));return U32(hr);}case 20:return output<LONG>(a[1],[&](auto* out){return video->get_Visible(out);});
        case 21:return U32(video->put_Left(integer(a[1])));case 22:return output<LONG>(a[1],[&](auto* out){return video->get_Left(out);});
        case 23:return U32(video->put_Width(integer(a[1])));case 24:return output<LONG>(a[1],[&](auto* out){return video->get_Width(out);});
        case 25:return U32(video->put_Top(integer(a[1])));case 26:return output<LONG>(a[1],[&](auto* out){return video->get_Top(out);});
        case 27:return U32(video->put_Height(integer(a[1])));case 28:return output<LONG>(a[1],[&](auto* out){return video->get_Height(out);});
        case 29:{const auto hr=video->put_Owner(window(a[1]));if(SUCCEEDED(hr))o.owner=a[1];return U32(hr);}
        case 30:{p.memory.check(a[1],4,Memory::Write);OAHWND value=0;const auto hr=video->get_Owner(&value);if(SUCCEEDED(hr)){if(value!=window(o.owner))stop("unmodeled native video owner");p.memory.store(a[1],o.owner,32);}return U32(hr);}
        case 31:{const auto hr=video->put_MessageDrain(window(a[1]));if(SUCCEEDED(hr))o.drain=a[1];return U32(hr);}
        case 32:{p.memory.check(a[1],4,Memory::Write);OAHWND value=0;const auto hr=video->get_MessageDrain(&value);if(SUCCEEDED(hr)){if(value!=window(o.drain))stop("unmodeled native video message drain");p.memory.store(a[1],o.drain,32);}return U32(hr);}
        case 33:return output<LONG>(a[1],[&](auto* out){return video->get_BorderColor(out);});case 34:return U32(video->put_BorderColor(integer(a[1])));
        case 35:return output<LONG>(a[1],[&](auto* out){return video->get_FullScreenMode(out);});case 36:return U32(video->put_FullScreenMode(integer(a[1])));case 37:return U32(video->SetWindowForeground(integer(a[1])));
        case 38:{const auto message=a[2];if(message!=WM_MOVE && message!=WM_SIZE && message!=WM_DISPLAYCHANGE && message!=WM_PAINT && !(message>=WM_KEYFIRST && message<=WM_KEYLAST) && !(message>=WM_MOUSEFIRST && message<=WM_MOUSELAST))stop("video owner notification payload");return U32(video->NotifyOwnerMessage(window(a[1]),integer(message),LONG_PTR(integer(a[3])),LONG_PTR(integer(a[4]))));}
        case 39:return U32(video->SetWindowPosition(integer(a[1]),integer(a[2]),integer(a[3]),integer(a[4])));
        case 40:return four(a,[&](LONG* out){return video->GetWindowPosition(out,out+1,out+2,out+3);});
        case 41:return two(a,[&](LONG* x,LONG* y){return video->GetMinIdealImageSize(x,y);});case 42:return two(a,[&](LONG* x,LONG* y){return video->GetMaxIdealImageSize(x,y);});
        case 43:return four(a,[&](LONG* out){return video->GetRestorePosition(out,out+1,out+2,out+3);});case 44:return U32(video->HideCursor(integer(a[1])));case 45:return output<LONG>(a[1],[&](auto* out){return video->IsCursorHidden(out);});
    }}
    if(o.kind==Kind::basic_video){auto* video=static_cast<IBasicVideo*>(o.native);switch(slot){
        case 7:return output<REFTIME>(a[1],[&](auto* out){return video->get_AvgTimePerFrame(out);});case 8:return output<LONG>(a[1],[&](auto* out){return video->get_BitRate(out);});case 9:return output<LONG>(a[1],[&](auto* out){return video->get_BitErrorRate(out);});
        case 10:return output<LONG>(a[1],[&](auto* out){return video->get_VideoWidth(out);});case 11:return output<LONG>(a[1],[&](auto* out){return video->get_VideoHeight(out);});
        case 12:return U32(video->put_SourceLeft(integer(a[1])));case 13:return output<LONG>(a[1],[&](auto* out){return video->get_SourceLeft(out);});case 14:return U32(video->put_SourceWidth(integer(a[1])));case 15:return output<LONG>(a[1],[&](auto* out){return video->get_SourceWidth(out);});
        case 16:return U32(video->put_SourceTop(integer(a[1])));case 17:return output<LONG>(a[1],[&](auto* out){return video->get_SourceTop(out);});case 18:return U32(video->put_SourceHeight(integer(a[1])));case 19:return output<LONG>(a[1],[&](auto* out){return video->get_SourceHeight(out);});
        case 20:return U32(video->put_DestinationLeft(integer(a[1])));case 21:return output<LONG>(a[1],[&](auto* out){return video->get_DestinationLeft(out);});case 22:return U32(video->put_DestinationWidth(integer(a[1])));case 23:return output<LONG>(a[1],[&](auto* out){return video->get_DestinationWidth(out);});
        case 24:return U32(video->put_DestinationTop(integer(a[1])));case 25:return output<LONG>(a[1],[&](auto* out){return video->get_DestinationTop(out);});case 26:return U32(video->put_DestinationHeight(integer(a[1])));case 27:return output<LONG>(a[1],[&](auto* out){return video->get_DestinationHeight(out);});
        case 28:return U32(video->SetSourcePosition(integer(a[1]),integer(a[2]),integer(a[3]),integer(a[4])));case 29:return four(a,[&](LONG* out){return video->GetSourcePosition(out,out+1,out+2,out+3);});case 30:return U32(video->SetDefaultSourcePosition());
        case 31:return U32(video->SetDestinationPosition(integer(a[1]),integer(a[2]),integer(a[3]),integer(a[4])));case 32:return four(a,[&](LONG* out){return video->GetDestinationPosition(out,out+1,out+2,out+3);});case 33:return U32(video->SetDefaultDestinationPosition());case 34:return two(a,[&](LONG* x,LONG* y){return video->GetVideoSize(x,y);});
        case 35:{if(a[2]>256)return U32(E_INVALIDARG);p.memory.check(a[3],4,Memory::Write);p.memory.check(a[4],a[2]*4,Memory::Write);std::vector<LONG> colors(a[2]);LONG count=0;const auto hr=video->GetVideoPaletteEntries(integer(a[1]),integer(a[2]),&count,colors.data());if(SUCCEEDED(hr)){if(count<0 || U32(count)>a[2])stop("invalid native video palette length");p.memory.store(a[3],U32(count),32);p.memory.copy_in(a[4],std::span(reinterpret_cast<const std::uint8_t*>(colors.data()),std::size_t(count)*4));}return U32(hr);}
        case 36:{p.memory.check(a[1],4,Memory::Read|Memory::Write);LONG size=integer(p.memory.load(a[1],32));if(size<0 || size>32*1024*1024)return U32(E_INVALIDARG);const auto capacity=U32(size);if(a[2])p.memory.check(a[2],capacity,Memory::Write);std::vector<LONG> data((capacity+3)/4);const auto hr=video->GetCurrentImage(&size,a[2]?data.data():nullptr);p.memory.store(a[1],U32(size),32);if(SUCCEEDED(hr) && a[2]){if(size<0 || U32(size)>capacity)stop("native video image overflow");p.memory.copy_in(a[2],std::span(reinterpret_cast<const std::uint8_t*>(data.data()),std::size_t(size)));}return U32(hr);}
        case 37:return U32(video->IsUsingDefaultSource());case 38:return U32(video->IsUsingDefaultDestination());
    }}
    if(o.kind==Kind::seeking){auto* seeking=static_cast<IMediaSeeking*>(o.native);switch(slot){
        case 3:return output<DWORD>(a[1],[&](auto* out){return seeking->GetCapabilities(out);});
        case 4:{auto value=read<DWORD>(p,a[1]);p.memory.check(a[1],4,Memory::Write);const auto hr=seeking->CheckCapabilities(&value);write(p,a[1],value);return U32(hr);}
        case 5:case 8:case 9:{auto value=read<GUID>(p,a[1]);return U32(slot==5?seeking->IsFormatSupported(&value):slot==8?seeking->IsUsingTimeFormat(&value):seeking->SetTimeFormat(&value));}
        case 6:return output<GUID>(a[1],[&](auto* out){return seeking->QueryPreferredFormat(out);});case 7:return output<GUID>(a[1],[&](auto* out){return seeking->GetTimeFormat(out);});
        case 10:return output<LONGLONG>(a[1],[&](auto* out){return seeking->GetDuration(out);});case 11:return output<LONGLONG>(a[1],[&](auto* out){return seeking->GetStopPosition(out);});case 12:return output<LONGLONG>(a[1],[&](auto* out){return seeking->GetCurrentPosition(out);});
        case 13:{std::optional<GUID> target=a[2]?std::optional(read<GUID>(p,a[2])):std::nullopt,source=a[5]?std::optional(read<GUID>(p,a[5])):std::nullopt;const auto time=std::bit_cast<LONGLONG>(std::uint64_t(a[3])|(std::uint64_t(a[4])<<32));return output<LONGLONG>(a[1],[&](auto* out){return seeking->ConvertTimeFormat(out,target?&*target:nullptr,time,source?&*source:nullptr);});}
        case 14:{LONGLONG current=0,end=0;if(a[1]){current=read<LONGLONG>(p,a[1]);p.memory.check(a[1],8,Memory::Write);}if(a[3]){end=read<LONGLONG>(p,a[3]);p.memory.check(a[3],8,Memory::Write);}const auto hr=seeking->SetPositions(a[1]?&current:nullptr,a[2],a[3]?&end:nullptr,a[4]);if(SUCCEEDED(hr)){if(a[1])write(p,a[1],current);if(a[3])write(p,a[3],end);}return U32(hr);}
        case 15:case 16:{p.memory.check(a[1],8,Memory::Write);p.memory.check(a[2],8,Memory::Write);LONGLONG first=0,second=0;const auto hr=slot==15?seeking->GetPositions(&first,&second):seeking->GetAvailable(&first,&second);if(SUCCEEDED(hr)){write(p,a[1],first);write(p,a[2],second);}return U32(hr);}
        case 17:return U32(seeking->SetRate(real(a,1)));case 18:return output<double>(a[1],[&](auto* out){return seeking->GetRate(out);});case 19:return output<LONGLONG>(a[1],[&](auto* out){return seeking->GetPreroll(out);});
    }}
    if(o.kind==Kind::position){auto* position=static_cast<IMediaPosition*>(o.native);switch(slot){
        case 7:return output<REFTIME>(a[1],[&](auto* out){return position->get_Duration(out);});case 8:return U32(position->put_CurrentPosition(real(a,1)));case 9:return output<REFTIME>(a[1],[&](auto* out){return position->get_CurrentPosition(out);});case 10:return output<REFTIME>(a[1],[&](auto* out){return position->get_StopTime(out);});case 11:return U32(position->put_StopTime(real(a,1)));case 12:return output<REFTIME>(a[1],[&](auto* out){return position->get_PrerollTime(out);});case 13:return U32(position->put_PrerollTime(real(a,1)));case 14:return U32(position->put_Rate(real(a,1)));case 15:return output<double>(a[1],[&](auto* out){return position->get_Rate(out);});case 16:return output<LONG>(a[1],[&](auto* out){return position->CanSeekForward(out);});case 17:return output<LONG>(a[1],[&](auto* out){return position->CanSeekBackward(out);});
    }}
    stop(std::string("unimplemented ")+std::to_string(unsigned(o.kind))+"::"+methods(o.kind)[slot].name);
}
}
void install_wa2_media(Process& p){auto state=std::make_shared<Media>(p);state->install();p.state().shutdown.push_back([state]{state->shutdown();});p.state().reports["directshow"]=[state]{return state->report();};}
}
