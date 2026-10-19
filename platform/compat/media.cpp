#include "media.hpp"
#include "media_abi.hpp"
#include "resource_mount.hpp"
#include "codepage.hpp"
#include "../../port/guest_api.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <deque>
#include <map>
#include <limits>
#include <set>
namespace wa2::platform::compat {namespace {
using namespace wr;using namespace wr::port;using graphics::abi::GUID;
constexpr U32 ok=0,no_interface=0x80004002u,invalid=0x80070057u,pointer_error=0x80004003u,no_event=0x80004004u;
constexpr U32 failure=0x80004005u,not_found=0x80040216u;
enum class Kind {unknown,graph,filter_graph,control,event,event_ex,audio,video_window,basic_video,seeking,position};
#include "../../port/media_tables.inc"
std::int64_t now_us(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::int32_t integer(U32 value){return std::bit_cast<std::int32_t>(value);}
double double_argument(Args a,unsigned n){return std::bit_cast<double>(std::uint64_t(a[n])|(std::uint64_t(a[n+1])<<32));}
struct Event {U32 code,first{},second{};auto operator<=>(const Event&)const=default;};
struct Graph {
    std::shared_ptr<resource::Source> source;movie::Information info;std::unique_ptr<movie::Playback> player;
    std::deque<Event> events;std::multiset<Event> delivered;std::set<U32> disabled_events;
    std::array<U32,46> properties{};graphics::MovieView view;
    std::u16string caption;std::shared_ptr<const void> last_frame;
    U32 event_handle{},references{},state{},notify_window{},notify_message{},notify_parameter{},notify_flags{};
    int volume{},balance{};bool completion{},shown{};std::int64_t start{},elapsed{},last_pump{},last_frame_time{-1};
};
struct Object {Kind kind;U32 references{1};std::shared_ptr<Graph> graph;};
class Media {
    Process& p;SharedAudio sound;std::shared_ptr<graphics::MoviePresenter> presenter;MovieServices services;
    std::map<U32,Object> objects;std::map<Kind,U32> tables;std::map<U32,U32> bstrs;
    std::vector<std::weak_ptr<Graph>> graphs;bool stopped{},pumping{};std::uint64_t created{},rendered{},runs{},completed{};
    std::uint64_t render_failures{};U32 last_render{},last_failure{};
    std::string last_file,last_error,last_failure_file,last_failure_error;
    std::int64_t clock()const{return services.monotonic_us();}
    [[noreturn]]void unsupported(const std::string& what){throw GuestFault(FaultKind::unsupported,p.cpu.eip,"shared movie: "+what);}
    template<class T>U32 output(U32 at,const T& value){write(p,at,value);return ok;}
    U32 kernel(const char* name,std::initializer_list<U32> args){const auto target=p.resolve("kernel32.dll",name);if(!target)unsupported(std::string("missing system service ")+name);return p.callback(target,{args.begin(),args.size()});}
    static std::optional<Kind> kind(const GUID& iid){
        if(iid==graphics::abi::IID_IUnknown)return Kind::unknown;
#define WA2_KIND(name) if(iid==media_abi::name)return Kind::name;
        WA2_KIND(graph) WA2_KIND(filter_graph) WA2_KIND(control) WA2_KIND(event) WA2_KIND(event_ex)
        WA2_KIND(audio) WA2_KIND(video_window) WA2_KIND(basic_video) WA2_KIND(seeking) WA2_KIND(position)
#undef WA2_KIND
        return {};
    }
    Object& object(U32 at){const auto found=objects.find(at);if(found==objects.end() || !found->second.references)unsupported("stale COM receiver");return found->second;}
    U32 vtable(Kind k){if(tables.contains(k))return tables.at(k);const auto info=methods(k);const auto table=p.allocate_bytes(info.size()*4);
        for(unsigned n=0;n<info.size();++n){const auto name="Movie."+std::to_string(unsigned(k))+"."+info[n].name;api(p,"wa2.media",name,info[n].arguments,[this,k,n](Process&,Args a){auto& o=object(a[0]);if(o.kind!=k)unsupported("wrong COM interface");pump();return method(o,n,a);});p.memory.store(table+n*4,p.resolve("wa2.media",name),32);}p.memory.protect(table,4096,Memory::Read);tables.emplace(k,table);return table;
    }
    U32 wrap(const std::shared_ptr<Graph>& graph,Kind k){for(auto& [at,value]:objects)if(value.graph==graph && value.kind==k){++value.references;++graph->references;return at;}
        if(objects.size()>=256)unsupported("movie interface budget");const auto at=p.allocate_bytes(4);p.memory.store(at,vtable(k),32);p.memory.protect(at,4096,Memory::Read);objects.emplace(at,Object{k,1,graph});++graph->references;return at;
    }
    U32 bstr(std::u16string_view text){if(text.size()>1048576)unsupported("BSTR size limit");const auto base=p.allocate_bytes(6+text.size()*2),at=base+4;p.memory.store(base,U32(text.size()*2),32);for(unsigned n=0;n<text.size();++n)p.memory.store(at+n*2,text[n],16);p.memory.store(at+U32(text.size()*2),0,16);bstrs.emplace(at,base);return at;}
    void stop(Graph& graph,bool release=false){if(graph.player)graph.player->stop();graph.player.reset();graph.state=0;graph.last_frame.reset();graph.last_frame_time=-1;
        if(graph.shown){presenter->restore_game();graph.shown=false;}if(release && graph.event_handle){kernel("CloseHandle",{graph.event_handle});graph.event_handle=0;}}
    U32 render_error(U32 hr,std::string error){++render_failures;last_render=last_failure=hr;last_error=std::move(error);last_failure_file=last_file;last_failure_error=last_error;return hr;}
    U32 render(Graph& graph,U32 filename){stop(graph);graph.source.reset();graph.info={};
        graph.completion=false;graph.elapsed=0;graph.events.clear();graph.delivered.clear();if(graph.event_handle)kernel("ResetEvent",{graph.event_handle});
        last_file.clear();last_error.clear();if(!filename)return render_error(pointer_error,"null movie filename");
        const auto name=p.read_wstring(filename);const auto encoded=encode_text(65001,name);last_file.assign(encoded.bytes.begin(),encoded.bytes.end());
        if(name.empty())return render_error(invalid,"empty movie filename");
        std::shared_ptr<resource::Source> source;
        try{source=services.open(p,name);}
        catch(const resource::NotFound& error){return render_error(not_found,error.what());}
        catch(const resource::Error& error){return render_error(failure,error.what());}
        // Unknown codecs, guest faults and unsupported operations still fail
        // explicitly; only ordinary source-open errors become HRESULTs.
        if(!source)unsupported("movie source opener returned no source");const auto info=services.inspect(source);if(!info.width || !info.height)unsupported("movie has no supported video track");
        graph.source=std::move(source);graph.info=info;
        graph.view.source={0,0,int(graph.info.width),int(graph.info.height)};if(graph.view.destination[2]<=0 || graph.view.destination[3]<=0)graph.view.destination=graph.view.source;
        ++rendered;return last_render=ok;
    }
    U32 run(Graph& graph){if(!graph.source)return 0x80040227u;if(graph.info.audio_rate && !sound.open_output())return 0x80040256u;
        const auto now=clock();if(graph.completion && graph.player)stop(graph);if(!graph.player){graph.player=services.player(graph.source,sound.mixer);if(!graph.player)unsupported("movie factory returned no playback");graph.player->gain(std::pow(10.f,float(graph.volume)/2000));if(graph.balance)unsupported("movie balance requires the PCM pan contract");graph.start=now;graph.elapsed=0;graph.completion=false;}
        else if(graph.state==1){graph.player->pause(false,now);graph.start=now-graph.elapsed;}
        graph.state=2;++runs;return ok;
    }
    void notify(Graph& graph,U32 code){if(graph.disabled_events.contains(code))return;if(graph.events.size()>=128)unsupported("movie event queue limit");graph.events.push_back({code});if(graph.event_handle)kernel("SetEvent",{graph.event_handle});
        if(graph.notify_window && !(graph.notify_flags&1)){const auto target=p.resolve("user32.dll","PostMessageA");if(!target)unsupported("movie notification has no window service");p.callback(target,std::array<U32,4>{graph.notify_window,graph.notify_message,0,graph.notify_parameter});}}
    U32 four(Args a,const std::array<int,4>& values){for(unsigned n=0;n<4;++n)p.memory.check(a[n+1],4,Memory::Write);for(unsigned n=0;n<4;++n)p.memory.store(a[n+1],U32(values[n]),32);return ok;}
    U32 two(Args a,int first,int second){p.memory.check(a[1],4,Memory::Write);p.memory.check(a[2],4,Memory::Write);p.memory.store(a[1],U32(first),32);p.memory.store(a[2],U32(second),32);return ok;}
    U32 seek_start(Graph& graph,std::int64_t position){if(position!=0)return 0x80004001u;const auto previous=graph.state;stop(graph);graph.elapsed=0;graph.completion=false;graph.events.clear();if(previous){const auto hr=run(graph);if(hr)return hr;if(previous==1){graph.player->pause(true,clock());graph.state=1;}}return ok;}
    U32 method(Object& o,unsigned slot,Args a){auto graph=o.graph;auto& g=*graph;
        if(slot==0){p.memory.check(a[2],4,Memory::Write);p.memory.store(a[2],0,32);const auto k=kind(read<GUID>(p,a[1]));if(!k)return no_interface;p.memory.store(a[2],wrap(graph,*k),32);return ok;}
        if(slot==1){++g.references;return ++o.references;}
        if(slot==2){const auto refs=--o.references;--g.references;if(!refs){p.memory.release(a[0]);objects.erase(a[0]);}if(!g.references)stop(g,true);return refs;}
        if(slot==3 && o.kind!=Kind::graph && o.kind!=Kind::filter_graph && o.kind!=Kind::seeking && o.kind!=Kind::unknown)return output(a[1],U32(0));
        if(o.kind==Kind::graph || o.kind==Kind::filter_graph){if(slot==10)return ok;if(slot==13 && o.kind==Kind::graph){if(a[2])return invalid;return render(g,a[1]);}if(slot==16){stop(g);return ok;}if(slot==17)return g.state?ok:U32(1);}
        if(o.kind==Kind::control)switch(slot){case 7:return run(g);case 8:if(g.player){g.player->pause(true,clock());g.elapsed=std::max<std::int64_t>(0,clock()-g.start);}g.state=1;return ok;case 9:case 15:stop(g);return ok;case 10:return output(a[2],g.state);case 11:return render(g,a[1]);}
        if(o.kind==Kind::event || o.kind==Kind::event_ex)switch(slot){
            case 7:if(!g.event_handle)g.event_handle=kernel("CreateEventA",{0,1,!g.events.empty(),0});return output(a[1],g.event_handle);
            case 8:{for(unsigned n=1;n<4;++n)p.memory.check(a[n],4,Memory::Write);if(g.events.empty() && a[4]){const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(integer(a[4]));if(integer(a[4])<0)unsupported("infinite movie GetEvent wait");while(g.events.empty() && std::chrono::steady_clock::now()<until){kernel("Sleep",{1});pump();}}
                Event value{};const auto hr=g.events.empty()?no_event:ok;if(!g.events.empty()){value=g.events.front();g.events.pop_front();g.delivered.insert(value);}for(unsigned n=1;n<4;++n)p.memory.store(a[n],n==1?value.code:n==2?value.first:value.second,32);if(g.events.empty() && g.event_handle)kernel("ResetEvent",{g.event_handle});return hr;}
            case 9:{const auto until=clock()+std::max(0,integer(a[1]))*1000ll;while(!g.completion && (a[1]==0xffffffffu || clock()<until)){kernel("Sleep",{1});pump();}return g.completion?output(a[2],U32(1)):no_event;}
            case 10:g.disabled_events.insert(a[1]);return ok;case 11:g.disabled_events.erase(a[1]);return ok;
            case 12:{const auto found=g.delivered.find({a[1],a[2],a[3]});if(found==g.delivered.end())return invalid;g.delivered.erase(found);return ok;}
            case 13:if(o.kind==Kind::event_ex){g.notify_window=a[1];g.notify_message=a[2];g.notify_parameter=a[3];return ok;}break;
            case 14:if(o.kind==Kind::event_ex){if(a[1]&~1u)return invalid;g.notify_flags=a[1];return ok;}break;
            case 15:if(o.kind==Kind::event_ex)return output(a[1],g.notify_flags);break;
        }
        if(o.kind==Kind::audio)switch(slot){case 7:if(integer(a[1])>0 || integer(a[1])<-10000)return invalid;g.volume=integer(a[1]);if(g.player)g.player->gain(std::pow(10.f,float(g.volume)/2000));return ok;case 8:return output(a[1],g.volume);case 9:if(integer(a[1])<-10000 || integer(a[1])>10000)return invalid;if(a[1])unsupported("movie balance requires the PCM pan contract");g.balance=0;return ok;case 10:return output(a[1],g.balance);}
        if(o.kind==Kind::video_window){
            if(slot==7){g.caption=p.read_wstring(a[1]);return ok;}if(slot==8)return output(a[1],bstr(g.caption));
            if(slot>=9 && slot<=28){if(slot%2){g.properties[slot]=a[1];if(slot==19){g.view.visible=a[1]!=0;if(!g.view.visible && g.shown){presenter->restore_game();g.shown=false;}}
                    if(slot==21)g.view.destination[0]=integer(a[1]);if(slot==23)g.view.destination[2]=integer(a[1]);if(slot==25)g.view.destination[1]=integer(a[1]);if(slot==27)g.view.destination[3]=integer(a[1]);return ok;}
                if(slot==22)return output(a[1],g.view.destination[0]);if(slot==24)return output(a[1],g.view.destination[2]);if(slot==26)return output(a[1],g.view.destination[1]);if(slot==28)return output(a[1],g.view.destination[3]);
                return output(a[1],g.properties[slot-1]);}
            switch(slot){case 29:g.view.owner=a[1];return ok;case 30:return output(a[1],g.view.owner);case 31:g.properties[31]=a[1];return ok;case 32:return output(a[1],g.properties[31]);
            case 33:return output(a[1],g.properties[34]);case 34:g.properties[34]=a[1];return ok;case 35:return output(a[1],g.properties[36]);case 36:g.properties[36]=a[1];return ok;
            case 37:if(g.view.owner && p.gui())p.callback(p.resolve("user32.dll","SetForegroundWindow"),std::array<U32,1>{g.view.owner});return ok;
            case 38:if(a[2]==0xf && g.player && g.view.visible)if(const auto frame=g.player->frame())presenter->movie_frame(*frame,g.view);return ok;
            case 39:for(unsigned n=0;n<4;++n)g.view.destination[n]=integer(a[n+1]);return ok;
            case 40:case 43:return four(a,g.view.destination);case 41:case 42:return two(a,int(g.info.width),int(g.info.height));
            case 44:if(const auto target=p.resolve("user32.dll","ShowCursor"))p.callback(target,std::array<U32,1>{a[1]?0u:1u});g.properties[44]=a[1];return ok;case 45:return output(a[1],g.properties[44]);}
        }
        if(o.kind==Kind::basic_video){
            if(slot==7)return output(a[1],double(0));if(slot==8 || slot==9)return output(a[1],U32(0));if(slot==10)return output(a[1],g.info.width);if(slot==11)return output(a[1],g.info.height);
            if(slot>=12 && slot<=27){const bool source=slot<20;const auto n=(slot-(source?12:20))/2;auto& rect=source?g.view.source:g.view.destination;const unsigned component=n==0?0:n==1?2:n==2?1:3;if(slot%2==0){rect[component]=integer(a[1]);return ok;}return output(a[1],rect[component]);}
            switch(slot){case 28:case 31:{auto& rect=slot==28?g.view.source:g.view.destination;for(unsigned n=0;n<4;++n)rect[n]=integer(a[n+1]);return ok;}case 29:return four(a,g.view.source);case 32:return four(a,g.view.destination);
            case 30:g.view.source={0,0,int(g.info.width),int(g.info.height)};return ok;case 33:g.view.destination={0,0,int(g.info.width),int(g.info.height)};return ok;case 34:return two(a,int(g.info.width),int(g.info.height));
            case 37:return U32(g.view.source!=std::array<int,4>{0,0,int(g.info.width),int(g.info.height)});case 38:return U32(g.view.destination!=std::array<int,4>{0,0,int(g.info.width),int(g.info.height)});}
        }
        if(o.kind==Kind::seeking)switch(slot){case 3:return output(a[1],U32(0x38));case 4:{const auto requested=p.memory.load(a[1],32),supported=requested&0x38;p.memory.store(a[1],supported,32);return requested==supported?ok:U32(1);}
            case 5:case 8:return read<GUID>(p,a[1])==media_abi::time_format?ok:U32(1);case 6:case 7:return output(a[1],media_abi::time_format);case 9:return read<GUID>(p,a[1])==media_abi::time_format?ok:invalid;
            case 10:case 11:return output(a[1],g.info.duration_us*10);case 12:return output(a[1],g.elapsed*10);
            case 14:if(a[4]&3)return 0x80004001u;if((a[2]&3)==0)return ok;if((a[2]&3)!=1 || !a[1])return invalid;return seek_start(g,read<std::int64_t>(p,a[1]));
            case 15:case 16:p.memory.check(a[1],8,Memory::Write);p.memory.check(a[2],8,Memory::Write);write(p,a[1],slot==15?g.elapsed*10:std::int64_t(0));write(p,a[2],g.info.duration_us*10);return ok;
            case 17:return double_argument(a,1)==1?ok:U32(0x80004001);case 18:return output(a[1],double(1));case 19:return output(a[1],std::int64_t(0));}
        if(o.kind==Kind::position)switch(slot){case 7:case 10:return output(a[1],double(g.info.duration_us)/1000000);case 8:if(double_argument(a,1)!=0)return 0x80004001u;return seek_start(g,0);case 9:return output(a[1],double(g.elapsed)/1000000);
            case 12:return output(a[1],double(0));case 14:return double_argument(a,1)==1?ok:U32(0x80004001);case 15:return output(a[1],double(1));case 16:case 17:return output(a[1],std::int32_t(0));}
        unsupported(std::string("interface method ")+std::to_string(unsigned(o.kind))+"::"+methods(o.kind)[slot].name);
    }
public:
    Media(Process& process,SharedAudio audio,std::shared_ptr<graphics::MoviePresenter> output,MovieServices supplied):p(process),sound(std::move(audio)),presenter(std::move(output)),services(std::move(supplied)){
        if(!sound.mixer || !sound.open_output || !presenter)throw std::invalid_argument("missing shared movie services");
        if(!services.monotonic_us)services.monotonic_us=now_us;
        if(!services.open)services.open=[](Process& p,std::u16string_view name){const auto encoded=encode_text(936,name).bytes;return open_process_resource(p,guest_file_path(p,std::string(encoded.begin(),encoded.end())));};
        if(!services.inspect)services.inspect=[](const std::shared_ptr<resource::Source>& source){return movie::Reader(source).information();};
        if(!services.player)services.player=[](auto source,auto mixer){return std::make_unique<movie::Player>(std::move(source),std::move(mixer));};
    }
    ~Media(){shutdown();}
    void pump(){if(stopped || pumping || graphs.empty())return;pumping=true;
        try{const auto now=clock();for(auto it=graphs.begin();it!=graphs.end();){const auto graph=it->lock();if(!graph){it=graphs.erase(it);continue;}++it;auto& g=*graph;
                if(!g.player || g.state!=2 || now-g.last_pump<2000)continue;g.last_pump=now;g.player->update(now);g.elapsed=std::min(std::max<std::int64_t>(0,now-g.start),g.info.duration_us>0?g.info.duration_us:std::numeric_limits<std::int64_t>::max());
                if(g.view.visible)if(const auto frame=g.player->frame();frame && (frame->owner!=g.last_frame || frame->time_us!=g.last_frame_time)){presenter->movie_frame(*frame,g.view);g.last_frame=frame->owner;g.last_frame_time=frame->time_us;g.shown=true;}
                if(g.player->finished() && !g.completion){g.completion=true;g.state=0;notify(g,1);++completed;}}
            pumping=false;
        }catch(...){pumping=false;throw;}
    }
    void shutdown()noexcept{if(stopped)return;stopped=true;for(auto& weak:graphs)if(auto graph=weak.lock())try{stop(*graph,true);}catch(...){}objects.clear();for(auto [at,base]:bstrs)try{p.memory.release(base);}catch(...){}bstrs.clear();}
    std::string report()const{return "{\"backend\":\"shared-native-movie\",\"graphs\":"+std::to_string(created)+",\"renders\":"+std::to_string(rendered)+",\"runs\":"+std::to_string(runs)+",\"completions\":"+std::to_string(completed)+",\"live_interfaces\":"+std::to_string(objects.size())+
        ",\"render_failures\":"+std::to_string(render_failures)+",\"last_render_hresult\":"+std::to_string(last_render)+",\"last_file\":"+wr::quote(last_file)+",\"last_error\":"+wr::quote(last_error)+
        ",\"last_failure_hresult\":"+std::to_string(last_failure)+",\"last_failure_file\":"+wr::quote(last_failure_file)+",\"last_failure_error\":"+wr::quote(last_failure_error)+"}";}
    void install(){
        api(p,"ole32.dll","CoCreateInstance",5,[this](Process&,Args a){if(!a[4])return pointer_error;p.memory.check(a[4],4,Memory::Write);p.memory.store(a[4],0,32);if(a[1])return U32(0x80040110);const auto clsid=read<GUID>(p,a[0]);if(clsid!=media_abi::filter_class && clsid!=media_abi::filter_no_thread)unsupported("COM class outside movie graph");const auto k=kind(read<GUID>(p,a[3]));if(!k)return no_interface;
            auto graph=std::make_shared<Graph>();graph->properties[13]=graph->properties[19]=0xffffffffu;graphs.push_back(graph);p.memory.store(a[4],wrap(graph,*k),32);++created;return ok;});
        api(p,"oleaut32.dll","SysAllocString",1,[this](Process&,Args a){return a[0]?bstr(p.read_wstring(a[0])):0;});
        api(p,"oleaut32.dll","SysAllocStringLen",2,[this](Process&,Args a){if(a[1]>1048576)return U32(0);std::u16string text(a[1],0);if(a[0])for(U32 n=0;n<a[1];++n)text[n]=char16_t(p.memory.load(a[0]+n*2,16));return bstr(text);});
        api(p,"oleaut32.dll","SysStringLen",1,[this](Process&,Args a){return a[0]?p.memory.load(a[0]-4,32)/2:0;});
        api(p,"oleaut32.dll","SysFreeString",1,[this](Process&,Args a){if(!a[0])return ok;const auto found=bstrs.find(a[0]);if(found==bstrs.end())unsupported("foreign BSTR");p.memory.release(found->second);bstrs.erase(found);return ok;});
    }
};
}
void install_media(wr::Process& p,wr::SharedAudio audio,std::shared_ptr<graphics::MoviePresenter> output,MovieServices services){auto state=std::make_shared<Media>(p,std::move(audio),std::move(output),std::move(services));state->install();p.state().shutdown.push_back([state]{state->shutdown();});p.state().poll.push_back([state]{state->pump();});p.state().reports["directshow"]=[state]{return state->report();};}
}

