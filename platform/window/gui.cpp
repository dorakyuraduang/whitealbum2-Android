#include "gui.hpp"
#include "pointer_coordinates.hpp"
#include "../compat/codepage.hpp"
#include "../../port/guest_api.hpp"
#include <winrecomp/resources.hpp>
#include <algorithm>
#include <bit>
#include <chrono>
#include <deque>
#include <map>
#include <set>
#include <limits>
#include <thread>
namespace wa2::window {namespace {
using wr::U32;using wr::Process;using wr::port::Args;using wr::port::read;using wr::port::write;
constexpr U32 WM_DESTROY=2,WM_CLOSE=0x10,WM_QUIT=0x12,WM_COMMAND=0x111,WM_INITDIALOG=0x110;
constexpr U32 WS_VISIBLE=0x10000000,WS_DISABLED=0x08000000;
struct Invalid {U32 code;};
struct Message {U32 window,message,wparam,lparam,time;std::int32_t x,y;};
static_assert(sizeof(Message)==28);
struct Class {U32 atom,procedure,style,extra,instance,menu;std::string name;};
struct Window {WindowSpec spec;U32 procedure{},user_data{},menu{},parent{},control{},dialog{};bool alive{true};std::vector<U32> extra;std::function<U32(U32,U32,U32)> system;};
struct Dialog {DialogState state;U32 procedure{},result{};bool ended{},modal{};std::map<U32,U32> handles;};
struct Accelerator {std::uint16_t flags,key,command,padding;};
struct Scratch {
    Process& p;U32 at{};explicit Scratch(Process& q):p(q),at(q.allocate_bytes(65536)){}
    ~Scratch(){try{p.memory.release(at);}catch(...){}}
};
class Gui final:public wr::GuiBackend {
    Process& p;std::shared_ptr<Host> host;const std::thread::id owner{std::this_thread::get_id()};
    std::map<U32,Window> windows;std::map<std::string,Class> classes;
    std::map<U32,Dialog> dialogs;std::map<U32,MenuTemplate> menus;
    std::map<U32,MenuTemplate> presented_menus;std::set<U32> dirty_menus;
    std::map<U32,std::vector<Accelerator>> accelerators;
    std::deque<Message> queue;std::map<U32,U32> dcs;
    U32 next{0xb1000000},atom{1},focus_window{},last_error{},quit_code{};bool stopped{},quit{};
    std::uint64_t callbacks{},dispatched{},created{},destroyed{};
    std::uint64_t menu_state_calls{},menu_presentations{},menu_unchanged{};
    [[noreturn]] void unsupported(const std::string& text){throw wr::GuestFault(wr::FaultKind::unsupported,p.cpu.eip,"shared window: "+text);}
    void enter(){if(stopped || std::this_thread::get_id()!=owner)unsupported("stopped or foreign-thread operation");}
    U32 token(){if(next>=0xb2000000)unsupported("window handle limit");auto value=next;next+=16;return value;}
    Window& window(U32 id){const auto it=windows.find(id);if(it==windows.end() || !it->second.alive)throw Invalid{1400};return it->second;}
    Dialog& dialog(U32 id){const auto it=dialogs.find(id);if(it==dialogs.end())throw Invalid{1400};return it->second;}
    std::u16string text(U32 at){if(!at)return {};const auto value=p.read_string(at);return platform::compat::decode_text(936,std::span(reinterpret_cast<const std::uint8_t*>(value.data()),value.size()));}
    std::string narrow(std::u16string_view value){const auto bytes=platform::compat::encode_text(936,value).bytes;return std::string(bytes.begin(),bytes.end());}
    U32 copy_text(U32 at,U32 capacity,std::u16string_view value){if(!capacity)return 0;const auto bytes=narrow(value);const auto count=std::min<std::size_t>(bytes.size(),capacity-1);
        p.memory.check(at,count+1,wr::Memory::Write);p.memory.copy_in(at,std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()),count));p.memory.store(at+U32(count),0,8);return U32(count);}
    wr::ResourceName name(U32 at){if(at<65536)return std::uint16_t(at);return text(at);}
    std::span<const std::uint8_t> resource(unsigned type,U32 id){const auto found=wr::find_resource(p.source_image(),std::uint16_t(type),name(id));if(!found)throw Invalid{1814};return wr::resource_bytes(p.source_image(),*found);}
    void bind(const char* name,unsigned count,std::function<U32(Args)> fn,U32 failure=0,const char* dll="user32.dll"){
        wr::port::api(p,dll,name,count,[this,fn=std::move(fn),failure](Process&,Args a){enter();try{return fn(a);}catch(const Invalid& error){p.set_error(error.code);return failure;}});
    }
    U32 call(U32 procedure,U32 id,U32 message,U32 wp,U32 lp){if(!procedure)return default_message(id,message,wp,lp);++callbacks;
        return p.callback(procedure,std::array<U32,4>{id,message,wp,lp});}
    U32 dispatch(const Message& message){++dispatched;if(!message.window)return 0;
        // A previously posted message may outlive its dialog. It must not
        // abort a later modal loop or call a retired source procedure.
        const auto retired=windows.find(message.window);if(retired!=windows.end() && !retired->second.alive)return 0;
        auto& target=window(message.window);
        const auto result=call(target.procedure,message.window,message.message,message.wparam,message.lparam);
        if(target.dialog && message.message==WM_CLOSE && !result){end_dialog(target.dialog,2);return 0;}return result;}
    std::array<std::int32_t,2> screen_cursor(std::array<std::int32_t,2> point,bool inverse=false)const{
        for(const auto& [id,w]:windows)if(w.alive && !w.parent && !w.control && !w.dialog && !w.system){
            point[0]+=inverse?-w.spec.bounds.x:w.spec.bounds.x;point[1]+=inverse?-w.spec.bounds.y:w.spec.bounds.y;break;}return point;
    }
    void post(U32 id,U32 message,U32 wp=0,U32 lp=0,std::optional<std::array<std::int32_t,2>> sample={}){if(queue.size()>=8192)unsupported("message queue limit");
        const auto cursor=screen_cursor(sample?*sample:host->cursor());const auto time=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-p.state().clock_start).count();
        if(sample && (message==0x20a || message==0x20e))lp=packed_pointer(cursor);
        queue.push_back({id,message,wp,lp,U32(time),cursor[0],cursor[1]});}
    void pump(unsigned timeout){auto changed=std::move(dirty_menus);dirty_menus.clear();for(const auto id:changed)refresh_menu(id);
        for(auto& event:host->events(timeout)){
        if(event.kind==Event::Kind::message){if(!event.window || windows.contains(event.window))post(event.window,event.message,event.wparam,event.lparam,event.client_cursor);continue;}
        auto& d=dialog(event.window);auto found=std::find_if(d.state.controls.begin(),d.state.controls.end(),[&](const auto& c){return c.definition.id==event.control;});
        if(found==d.state.controls.end())unsupported("event for an unknown dialog control");
        if(event.kind==Event::Kind::text)found->text=std::move(event.text);
        else if(event.kind==Event::Kind::selection)found->selection=event.value;
        else if(event.kind==Event::Kind::position)found->position=std::clamp(event.value,found->minimum,found->maximum);
        else found->check=event.value;
    }}
    U32 get_message(Args a,bool blocking){
        if(a[1])window(a[1]);if(a[2]>a[3] && a[3])throw Invalid{87};p.memory.check(a[0],28,wr::Memory::Write);
        const auto matches=[&](const Message& m){return m.message==WM_QUIT || ((!a[1] || m.window==a[1]) && ((!a[2] && !a[3]) || (m.message>=a[2] && m.message<=a[3])));};
        for(;;){pump(0);const auto found=std::find_if(queue.begin(),queue.end(),matches);
            if(found!=queue.end()){const auto value=*found;write(p,a[0],value);if(blocking || (a[4]&1))queue.erase(found);return blocking?(value.message==WM_QUIT?0:1):1;}
            if(!blocking)return 0;pump(16);
        }
    }
    Class& klass(U32 at){if(at<65536){for(auto& [key,value]:classes)if(value.atom==at)return value;}else if(const auto found=classes.find(p.read_string(at,256));found!=classes.end())return found->second;throw Invalid{1407};}
    U32 create_window(Args a){auto& c=klass(a[1]);if(a[8])window(a[8]);
        const auto id=token();Window value;value.spec={id,a[8],a[3],a[0],{std::bit_cast<std::int32_t>(a[4]),std::bit_cast<std::int32_t>(a[5]),std::bit_cast<std::int32_t>(a[6]),std::bit_cast<std::int32_t>(a[7])},text(a[2])};
        if(!(value.spec.style&0x80000000u)){
            if(value.spec.bounds.width<=0)value.spec.bounds.width=1280;if(value.spec.bounds.height<=0)value.spec.bounds.height=720;
        }else if(value.spec.bounds.width<0 || value.spec.bounds.height<0)throw Invalid{87};
        if(value.spec.bounds.x==std::numeric_limits<std::int32_t>::min())value.spec.bounds.x=std::max(0,(host->desktop().width-value.spec.bounds.width)/2);
        if(value.spec.bounds.y==std::numeric_limits<std::int32_t>::min())value.spec.bounds.y=std::max(0,(host->desktop().height-value.spec.bounds.height)/2);
        value.procedure=c.procedure;value.extra.resize((c.extra+3)/4);value.parent=a[8];value.menu=a[9];
        if(!value.menu && c.menu){const auto menu=token();menus.emplace(menu,parse_menu(resource(4,c.menu)));value.menu=menu;}
        windows.emplace(id,std::move(value));host->create(windows.at(id).spec);++created;
        Scratch scratch(p);const std::array<U32,12> create{a[11],a[10],a[9],a[8],a[7],a[6],a[5],a[4],a[3],a[2],a[1],a[0]};write(p,scratch.at,create);
        if(!call(c.procedure,id,0x81,0,scratch.at)){destroy_window(id);return 0;}
        if(call(c.procedure,id,1,0,scratch.at)==0xffffffffu){destroy_window(id);return 0;}
        if(windows.at(id).menu)present_menu(id,menus.at(windows.at(id).menu));
        post(id,5,0,U32(windows.at(id).spec.bounds.width&65535)|(U32(windows.at(id).spec.bounds.height&65535)<<16));return id;
    }
    void destroy_window(U32 id){auto& w=window(id);if(w.control)throw Invalid{87};call(w.procedure,id,WM_DESTROY,0,0);call(w.procedure,id,0x82,0,0);w.alive=false;host->destroy(id);++destroyed;
        presented_menus.erase(id);
        if(focus_window==id)focus_window=0;for(auto& [child,value]:windows)if(value.parent==id)value.alive=false;}
    U32 default_message(U32 id,U32 message,U32 wp,U32 lp){
        if(message==0x81 || message==0x14 || message==0x20 || message==0x21 || message==0x84)return 1;
        if(message==WM_CLOSE){destroy_window(id);return 0;}
        if(message==0x0c){set_text(id,text(lp));return 1;}
        if(message==0x0d)return copy_text(lp,wp,get_text(id));if(message==0x0e)return U32(narrow(get_text(id)).size());
        if(message==0x87)return 0;return 0;
    }
    ControlState& control(U32 id){const auto& w=window(id);if(!w.control)throw Invalid{1400};auto& d=dialog(w.dialog);
        for(auto& c:d.state.controls)if(c.definition.id==w.control)return c;throw Invalid{1400};}
    std::u16string get_text(U32 id){auto& w=window(id);return w.control?control(id).text:w.spec.title;}
    void update_control(U32 id){host->update_dialog(dialog(window(id).dialog).state);}
    void set_text(U32 id,std::u16string value){auto& w=window(id);if(w.control){control(id).text=std::move(value);update_control(id);}else {w.spec.title=std::move(value);host->title(id,w.spec.title);}}
    U32 control_message(U32 id,U32 message,U32 wp,U32 lp){auto& c=control(id);U32 result{};bool changed=true;
        switch(message){
        case 0x0b:c.enabled=wp!=0;break;case 0x0c:c.text=text(lp);result=1;break;
        case 0x0d:return copy_text(lp,wp,c.text);case 0x0e:return U32(narrow(c.text).size());
        case 0xf0:return U32(c.check);case 0xf1:if(wp>2)throw Invalid{87};c.check=int(wp);break;
        case 0xf5:{const auto& w=window(id);post(w.parent,WM_COMMAND,w.control,id);changed=false;break;}
        case 0x180:case 0x143:if(c.items.size()>=4096)unsupported("list control item limit");result=U32(c.items.size());c.items.push_back(text(lp));break;
        case 0x181:case 0x14a:if(wp>c.items.size())return 0xffffffffu;c.items.insert(c.items.begin()+wp,text(lp));result=wp;break;
        case 0x182:case 0x144:if(wp>=c.items.size())return 0xffffffffu;c.items.erase(c.items.begin()+wp);result=U32(c.items.size());break;
        case 0x184:case 0x14b:c.items.clear();c.selection=-1;break;
        case 0x185:c.selection=wp?std::bit_cast<std::int32_t>(lp):-1;break;
        case 0x186:case 0x14e:c.selection=std::bit_cast<std::int32_t>(wp);if(c.selection<-1 || c.selection>=std::int64_t(c.items.size()))return 0xffffffffu;result=wp;break;
        case 0x188:case 0x147:return U32(c.selection);
        case 0x18b:case 0x146:return U32(c.items.size());
        case 0x18a:case 0x149:if(wp>=c.items.size())return 0xffffffffu;return U32(narrow(c.items[wp]).size());
        case 0x189:case 0x148:if(wp>=c.items.size())return 0xffffffffu;return copy_text(lp,U32(narrow(c.items[wp]).size()+1),c.items[wp]);
        case 0x400:return U32(c.position);case 0x401:return U32(c.minimum);case 0x402:return U32(c.maximum);
        case 0x405:c.position=std::clamp(std::bit_cast<std::int32_t>(lp),c.minimum,c.maximum);break;
        case 0x406:c.minimum=std::int16_t(lp);c.maximum=std::int16_t(lp>>16);if(c.minimum>c.maximum)throw Invalid{87};c.position=std::clamp(c.position,c.minimum,c.maximum);break;
        case 0x407:c.minimum=std::bit_cast<std::int32_t>(lp);if(c.minimum>c.maximum)throw Invalid{87};c.position=std::max(c.position,c.minimum);break;
        case 0x408:c.maximum=std::bit_cast<std::int32_t>(lp);if(c.maximum<c.minimum)throw Invalid{87};c.position=std::min(c.position,c.maximum);break;
        case 0x409:c.ticks.clear();break;
        case 0x404:if(c.ticks.size()>=4096)unsupported("slider tick limit");c.ticks.push_back(std::bit_cast<std::int32_t>(lp));result=1;break;
        case 0x403:return wp<c.ticks.size()?U32(c.ticks[wp]):0xffffffffu;
        case 0x40a:c.selection_start=std::int16_t(lp);c.selection_end=std::int16_t(lp>>16);break;
        case 0x40b:c.selection_start=std::bit_cast<std::int32_t>(lp);break;
        case 0x40c:c.selection_end=std::bit_cast<std::int32_t>(lp);break;
        case 0x410:return U32(c.ticks.size()+2);
        case 0x411:return U32(c.selection_start);case 0x412:return U32(c.selection_end);
        case 0x413:c.selection_start=c.selection_end=0;break;
        case 0x414:c.ticks.clear();if(wp)for(std::int64_t value=std::int64_t(c.minimum)+wp;value<c.maximum && c.ticks.size()<4096;value+=wp)c.ticks.push_back(std::int32_t(value));break;
        case 0x415:result=U32(c.page_size);c.page_size=std::max(1,std::bit_cast<std::int32_t>(lp));break;
        case 0x416:return U32(c.page_size);
        case 0x417:result=U32(c.line_size);c.line_size=std::max(1,std::bit_cast<std::int32_t>(lp));break;
        case 0x418:return U32(c.line_size);
        case 0x30:c.font=wp;break;
        default:unsupported("control message "+std::to_string(message));
        }if(changed)update_control(id);return result;
    }
    U32 send(U32 id,U32 message,U32 wp,U32 lp){auto& w=window(id);if(w.system)return w.system(message,wp,lp);return w.control?control_message(id,message,wp,lp):call(w.procedure,id,message,wp,lp);}
    void end_dialog(U32 id,U32 result){auto& d=dialog(id);d.result=result;d.ended=true;d.state.visible=false;host->show(id,false);}
    U32 create_dialog(Args a,bool modal){if(a[2])window(a[2]);const auto definition=parse_dialog(resource(5,a[1]));const auto id=token();Dialog d;d.state={id,a[2],definition,{}};d.state.visible=modal || (definition.style&WS_VISIBLE)!=0;d.procedure=a[3];d.modal=modal;
        Window w;w.spec={id,a[2],definition.style,definition.extended_style,{},definition.title};w.parent=a[2];w.procedure=a[3];w.dialog=id;w.extra.resize(3);
        windows.emplace(id,std::move(w));
        for(const auto& control:definition.controls){ControlState state;state.definition=control;if(const auto* title=std::get_if<std::u16string>(&control.title))state.text=*title;
            if(const auto* type=std::get_if<std::uint16_t>(&control.type);type && *type==130 && (control.style&31)==3 && !std::holds_alternative<std::monostate>(control.title))
                state.icon=load_icon_resource(p.source_image(),control.title);
            state.enabled=(control.style&WS_DISABLED)==0;state.visible=(control.style&WS_VISIBLE)!=0;d.state.controls.push_back(std::move(state));
            const auto child=token();d.state.controls.back().token=child;Window child_window;child_window.spec.token=child;child_window.parent=id;child_window.control=control.id;child_window.dialog=id;
            windows.emplace(child,std::move(child_window));d.handles.emplace(control.id,child);}
        dialogs.emplace(id,std::move(d));host->create_dialog(dialogs.at(id).state);++created;
        call(a[3],id,WM_INITDIALOG,0,a[4]);host->update_dialog(dialogs.at(id).state);
        if(!modal)return id;
        auto& current=dialogs.at(id);while(!current.ended && !quit){pump(16);while(!queue.empty() && !current.ended){const auto event=queue.front();queue.pop_front();if(event.message==WM_QUIT){quit=true;quit_code=event.wparam;break;}dispatch(event);}}
        const auto result=current.result;destroy_window(id);return result;
    }
    MenuItem* menu_item(std::vector<MenuItem>& items,U32 item,bool by_position){if(by_position)return item<items.size()?&items[item]:nullptr;
        for(auto& value:items){if(value.id==item)return &value;if(auto* found=menu_item(value.children,item,false))return found;}return nullptr;}
    MenuTemplate& menu(U32 id){const auto found=menus.find(id);if(found==menus.end())throw Invalid{1401};return found->second;}
    void present_menu(U32 id,const MenuTemplate& value){const auto previous=presented_menus.find(id);
        if(previous!=presented_menus.end() && previous->second==value){++menu_unchanged;return;}
        host->menu(id,value);presented_menus[id]=value;++menu_presentations;
    }
    void refresh_menu(U32 id){for(const auto& [window_id,w]:windows)if(w.alive && w.menu==id)present_menu(window_id,menu(id));}
    U32 menu_change(Args a,unsigned operation){auto& model=menu(a[0]);const bool by_position=(a[2]&0x400)!=0;
        auto* item=menu_item(model.items,a[1],by_position);if(operation<2){++menu_state_calls;if(!item)return 0xffffffffu;const auto previous=item->state;
            if(operation==0)item->state=(item->state&~3u)|(a[2]&3u);else item->state=(item->state&~8u)|(a[2]&8u);
            if(item->state!=previous)dirty_menus.insert(a[0]);else ++menu_unchanged;return operation==0?(previous&3u):(previous&8u);}
        if(a[2]&(4|0x100|0x4000))unsupported("bitmap or owner-drawn menu");
        if(operation==2){if(!item)return 0;MenuItem value;value.id=a[3];value.state=a[2]&0xb;value.type=a[2]&0x800;value.text=(a[2]&0x800)?std::u16string{}:text(a[4]);
            if(a[2]&0x10)value.children=menu(a[3]).items;*item=std::move(value);}
        else if(operation==3){MenuItem value;value.id=a[3];value.state=a[2]&0xb;value.type=a[2]&0x800;value.text=(a[2]&0x800)?std::u16string{}:text(a[4]);if(a[2]&0x10)value.children=menu(a[3]).items;
            auto position=by_position?std::min<std::size_t>(a[1],model.items.size()):model.items.size();
            if(!by_position && item){const auto root=std::find_if(model.items.begin(),model.items.end(),[&](const auto& value){return &value==item;});if(root==model.items.end())unsupported("nested menu insertion");position=std::size_t(root-model.items.begin());}
            model.items.insert(model.items.begin()+position,std::move(value));}
        else{if(!item)return 0;const auto erase=[&](auto&& self,std::vector<MenuItem>& list)->bool{for(auto it=list.begin();it!=list.end();++it){if(&*it==item){list.erase(it);return true;}if(self(self,it->children))return true;}return false;};erase(erase,model.items);}
        dirty_menus.insert(a[0]);return 1;
    }
public:
    Gui(Process& process,std::shared_ptr<Host> h):p(process),host(std::move(h)){if(!host)throw std::invalid_argument("window host required");install();}
    ~Gui() override{shutdown();}
    void shutdown() noexcept override {if(stopped)return;stopped=true;for(auto& [id,w]:windows)if(w.alive && !w.control && !w.system)try{host->destroy(id);}catch(...){}windows.clear();dialogs.clear();queue.clear();}
    U32 attach_system_dialog(std::function<U32(U32,U32,U32)> handler)override{enter();const auto id=token();Window value;value.spec.token=id;value.system=std::move(handler);windows.emplace(id,std::move(value));return id;}
    void detach_system_dialog(U32 id)override{enter();if(!window(id).system)throw Invalid{1400};windows.erase(id);}
    std::string report() const override{return "{\"backend\":\"shared-window-model\",\"created\":"+std::to_string(created)+",\"destroyed\":"+std::to_string(destroyed)+",\"callbacks\":"+std::to_string(callbacks)+",\"dispatched\":"+std::to_string(dispatched)+
        ",\"menu_state_calls\":"+std::to_string(menu_state_calls)+",\"menu_presentations\":"+std::to_string(menu_presentations)+",\"menu_unchanged\":"+std::to_string(menu_unchanged)+",\"input\":"+host->input_report()+"}";}
    void install(){
        bind("RegisterClassA",1,[&](Args a){const auto fields=read<std::array<U32,10>>(p,a[0]);const auto key=p.read_string(fields[9],256);if(classes.contains(key)){p.set_error(1410);return U32(0);}if(fields[2]>4096 || fields[3]>4096)throw Invalid{87};
            const auto id=atom++;classes.emplace(key,Class{id,fields[1],fields[0],fields[3],fields[4],fields[8],key});return id;});
        bind("CreateWindowExA",12,[&](Args a){return create_window(a);});
        bind("DestroyWindow",1,[&](Args a){destroy_window(a[0]);return U32(1);});
        bind("DefWindowProcA",4,[&](Args a){return default_message(a[0],a[1],a[2],a[3]);});
        bind("SendMessageA",4,[&](Args a){return send(a[0],a[1],a[2],a[3]);});
        bind("PostMessageA",4,[&](Args a){if(a[0])window(a[0]);post(a[0],a[1],a[2],a[3]);return U32(1);});
        bind("PostQuitMessage",1,[&](Args a){post(0,WM_QUIT,a[0]);return U32(0);});
        bind("PeekMessageA",5,[&](Args a){return get_message(a,false);});
        bind("GetMessageA",4,[&](Args a){const std::array<U32,5> values{a[0],a[1],a[2],a[3],1};return get_message(values,true);},0xffffffffu);
        bind("DispatchMessageA",1,[&](Args a){return dispatch(read<Message>(p,a[0]));});
        bind("TranslateMessage",1,[&](Args a){const auto m=read<Message>(p,a[0]);return U32(m.message>=0x100 && m.message<=0x109);});
        bind("ShowWindow",2,[&](Args a){auto& w=window(a[0]);const bool previous=host->visible(a[0]);const bool visible=a[1]!=0;host->show(a[0],visible);if(dialogs.contains(a[0]))dialogs.at(a[0]).state.visible=visible;w.spec.style=(w.spec.style&~WS_VISIBLE)|(visible?WS_VISIBLE:0);post(a[0],0x18,visible);return U32(previous);});
        bind("IsWindowVisible",1,[&](Args a){window(a[0]);return U32(host->visible(a[0]));});
        bind("UpdateWindow",1,[&](Args a){window(a[0]);send(a[0],0xf,0,0);return U32(1);});
        bind("SetWindowTextA",2,[&](Args a){set_text(a[0],text(a[1]));return U32(1);});
        bind("SetDlgItemTextA",3,[&](Args a){const auto& d=dialog(a[0]);const auto found=d.handles.find(a[1]);if(found==d.handles.end())throw Invalid{1421};set_text(found->second,text(a[2]));return U32(1);});
        bind("GetDlgItem",2,[&](Args a){const auto& d=dialog(a[0]);const auto found=d.handles.find(a[1]);return found==d.handles.end()?0:found->second;});
        bind("SendDlgItemMessageA",5,[&](Args a){const auto& d=dialog(a[0]);const auto found=d.handles.find(a[1]);if(found==d.handles.end())throw Invalid{1421};return send(found->second,a[2],a[3],a[4]);});
        bind("DialogBoxParamA",5,[&](Args a){return create_dialog(a,true);},0xffffffffu);
        bind("CreateDialogParamA",5,[&](Args a){return create_dialog(a,false);});
        bind("EndDialog",2,[&](Args a){end_dialog(a[0],a[1]);return U32(1);});
        bind("SetWindowLongA",3,[&](Args a){auto& w=window(a[0]);const auto index=std::bit_cast<std::int32_t>(a[1]);U32* field{};
            if(index==-4)field=&w.procedure;else if(index==-21)field=&w.user_data;else if(index==-16)field=&w.spec.style;else if(index==-20)field=&w.spec.extended_style;
            else if(index>=0 && !(index&3) && U32(index)/4<w.extra.size())field=&w.extra[U32(index)/4];else throw Invalid{1413};const auto old=*field;*field=a[2];return old;});
        bind("GetClientRect",2,[&](Args a){const auto bounds=host->client(a[0]);write(p,a[1],std::array<std::int32_t,4>{0,0,bounds.width,bounds.height});return U32(1);});
        bind("GetWindowRect",2,[&](Args a){const auto bounds=a[0]==0xb0fffff0?host->desktop():window(a[0]).spec.bounds;write(p,a[1],std::array<std::int32_t,4>{bounds.x,bounds.y,bounds.x+bounds.width,bounds.y+bounds.height});return U32(1);});
        bind("SetRect",5,[&](Args a){write(p,a[0],std::array<U32,4>{a[1],a[2],a[3],a[4]});return U32(1);});
        bind("AdjustWindowRect",3,[&](Args a){p.memory.check(a[0],16,wr::Memory::Read|wr::Memory::Write);return U32(1);});
        bind("SetWindowPos",7,[&](Args a){auto& w=window(a[0]);if(!(a[6]&2)){w.spec.bounds.x=std::bit_cast<std::int32_t>(a[2]);w.spec.bounds.y=std::bit_cast<std::int32_t>(a[3]);}if(!(a[6]&1)){w.spec.bounds.width=int(a[4]);w.spec.bounds.height=int(a[5]);}host->position(a[0],w.spec.bounds,a[6]);return U32(1);});
        bind("GetDesktopWindow",0,[&](Args){return U32(0xb0fffff0);});
        bind("GetSystemMetrics",1,[&](Args a){if(const auto value=host->system_metric(std::bit_cast<std::int32_t>(a[0])))return U32(*value);unsupported("system metric "+std::to_string(a[0]));});
        bind("FindWindowA",2,[&](Args a){for(const auto& [id,w]:windows)if(w.alive && !w.control && (!a[1] || w.spec.title==text(a[1]))){if(!a[0])return id;const auto& c=klass(a[0]);if(c.procedure==w.procedure)return id;}return U32(0);});
        bind("GetForegroundWindow",0,[&](Args){for(const auto& [id,w]:windows)if(w.alive && !w.control && host->focused(id))return id;return U32(0);});
        bind("SetForegroundWindow",1,[&](Args a){window(a[0]);host->focus(a[0]);focus_window=a[0];return U32(1);});
        bind("SetFocus",1,[&](Args a){const auto previous=focus_window;if(a[0]){window(a[0]);host->focus(a[0]);}focus_window=a[0];return previous;});
        bind("GetWindowThreadProcessId",2,[&](Args a){window(a[0]);if(a[1])p.memory.store(a[1],1,32);return p.state().current_thread_id;});
        bind("AttachThreadInput",3,[&](Args a){if(a[0]!=a[1] && a[0] && a[1])unsupported("cross-process input attachment");return U32(1);});
        bind("GetCursorPos",1,[&](Args a){pump(0);const auto point=screen_cursor(host->cursor());write(p,a[0],point);return U32(1);});
        bind("SetCursorPos",2,[&](Args a){const auto point=screen_cursor({std::bit_cast<std::int32_t>(a[0]),std::bit_cast<std::int32_t>(a[1])},true);host->cursor(point[0],point[1]);return U32(1);});
        for(bool screen:{false,true})bind(screen?"ClientToScreen":"ScreenToClient",2,[&,screen](Args a){const auto& bounds=window(a[0]).spec.bounds;auto point=read<std::array<std::int32_t,2>>(p,a[1]);point[0]+=screen?bounds.x:-bounds.x;point[1]+=screen?bounds.y:-bounds.y;write(p,a[1],point);return U32(1);});
        bind("ShowCursor",1,[&](Args a){return U32(host->show_cursor(a[0]!=0));});
        bind("GetAsyncKeyState",1,[&](Args a){pump(0);return U32(std::int32_t(std::int16_t(host->key(a[0]))));});
        bind("WINNLSEnableIME",2,[&](Args a){window(a[0]);return U32(1);});
        bind("MessageBoxA",4,[&](Args a){return host->message_box(text(a[1]),text(a[2]),a[3]);});
        bind("LoadIconA",2,[&](Args a){if(a[1]>=65536)resource(14,a[1]);return U32(0xb0f00000|(a[1]&65535));});
        bind("LoadCursorA",2,[&](Args a){if(a[0])unsupported("custom cursor resource");return U32(0xb0e00000|(a[1]&65535));});
        bind("LoadMenuA",2,[&](Args a){const auto id=token();menus.emplace(id,parse_menu(resource(4,a[1])));return id;});
        bind("GetMenu",1,[&](Args a){return window(a[0]).menu;});
        bind("SetMenu",2,[&](Args a){if(a[1])menu(a[1]);window(a[0]).menu=a[1];present_menu(a[0],a[1]?menu(a[1]):MenuTemplate{});return U32(1);});
        bind("DrawMenuBar",1,[&](Args a){const auto id=window(a[0]).menu;if(id){dirty_menus.erase(id);refresh_menu(id);}return U32(1);});
        bind("EnableMenuItem",3,[&](Args a){return menu_change(a,0);},0xffffffffu);
        bind("CheckMenuItem",3,[&](Args a){return menu_change(a,1);},0xffffffffu);
        bind("ModifyMenuA",5,[&](Args a){return menu_change(a,2);});
        bind("InsertMenuA",5,[&](Args a){return menu_change(a,3);});
        bind("DeleteMenu",3,[&](Args a){return menu_change(a,4);});
        bind("LoadAcceleratorsA",2,[&](Args a){const auto bytes=resource(9,a[1]);if(bytes.size()%8)unsupported("accelerator resource size");std::vector<Accelerator> values(bytes.size()/8);std::memcpy(values.data(),bytes.data(),bytes.size());const auto id=token();accelerators.emplace(id,std::move(values));return id;});
        bind("TranslateAcceleratorA",3,[&](Args a){window(a[0]);const auto found=accelerators.find(a[1]);if(found==accelerators.end())throw Invalid{6};const auto message=read<Message>(p,a[2]);if(message.message!=0x100 && message.message!=0x104)return U32(0);
            for(const auto& item:found->second)if(item.key==message.wparam && bool(item.flags&4)==bool(host->key(0x10)&0x8000) && bool(item.flags&8)==bool(host->key(0x11)&0x8000) && bool(item.flags&16)==bool(host->key(0x12)&0x8000)){send(a[0],WM_COMMAND,U32(item.command)|0x10000u,0);return U32(1);}return U32(0);});
        bind("GetDC",1,[&](Args a){if(a[0])window(a[0]);const auto id=token();dcs.emplace(id,a[0]);return id;});
        bind("ReleaseDC",2,[&](Args a){const auto found=dcs.find(a[1]);if(found==dcs.end() || found->second!=a[0])return U32(0);dcs.erase(found);return U32(1);});
        bind("GetDeviceCaps",2,[&](Args a){if(!dcs.contains(a[0]))throw Invalid{6};const auto bounds=host->desktop();switch(a[1]){case 2:return U32(1);case 8:return U32(bounds.width);case 10:return U32(bounds.height);case 12:return U32(32);case 14:return U32(1);case 88:case 90:return U32(96);case 104:case 106:return U32(0);default:unsupported("device capability "+std::to_string(a[1]));}},0,"gdi32.dll");
        bind("GetStockObject",1,[&](Args a){if(a[0]>19)throw Invalid{87};return U32(0xb0d00000|a[0]);},0,"gdi32.dll");
    }
};
}
std::unique_ptr<wr::GuiBackend> install(wr::Process& p,std::shared_ptr<Host> host){return std::make_unique<Gui>(p,std::move(host));}
}
