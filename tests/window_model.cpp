#include "process.hpp"
#include "../platform/window/gui.hpp"
#include "../platform/window/hidden_popups.hpp"
#include "../platform/window/pointer_coordinates.hpp"
#include "../platform/compat/legacy_display.hpp"
#include "../generated/graphics-abi/source_abi.hpp"
#include "../port/guest_api.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
using wr::U32;namespace w=wa2::window;
class MemoryHost final:public w::Host {
public:
    std::map<U32,w::WindowSpec> windows;std::map<U32,w::DialogState> dialogs;std::vector<w::Event> pending;
    unsigned frames{},menus{};U32 focus_id{};std::array<std::int32_t,2> mouse{};int cursor_count{};
    w::MenuTemplate last_menu;
    std::array<std::uint16_t,256> keys{};
    w::HiddenPopups popups;bool native_metrics{true};
    SDL_Window* acquire(U32,U32,U32,bool) override{throw std::runtime_error("memory host cannot create a graphics device");}
    bool embedded_gl()const noexcept override{return false;}void frame_submitted()override{++frames;}
    void create(const w::WindowSpec& s)override{CHECK(!windows.contains(s.token));if(!windows.empty()){popups.create(s,windows.begin()->first);return;}windows.emplace(s.token,s);}
    void destroy(U32 id)override{if(popups.contains(id)){popups.destroy(id);return;}windows.erase(id);dialogs.erase(id);}
    void show(U32 id,bool visible)override{if(dialogs.contains(id))dialogs.at(id).visible=visible;else if(popups.contains(id))popups.show(id,visible);else windows.at(id).style=(windows.at(id).style&~0x10000000u)|(visible?0x10000000:0);}
    void title(U32 id,std::u16string_view title)override{if(dialogs.contains(id))dialogs.at(id).definition.title=title;else if(popups.contains(id))popups.get(id).title=title;else windows.at(id).title=title;}
    void position(U32 id,const w::Bounds& b,U32 flags)override{if(popups.contains(id)){popups.position(id,b,flags);return;}windows.at(id).bounds=b;}
    w::Bounds client(U32 id)const override{if(popups.contains(id))return popups.get(id).bounds;return windows.at(id).bounds;}
    w::Bounds desktop()const override{return {0,0,1920,1080};}
    std::optional<std::int32_t> system_metric(std::int32_t index)const override{if(native_metrics && (index==54 || index==55))return index==54?29:27;return Host::system_metric(index);}
    bool visible(U32 id)const override{if(dialogs.contains(id))return dialogs.at(id).visible;if(popups.contains(id))return false;return (windows.at(id).style&0x10000000)!=0;}
    bool focused(U32 id)const override{return id==focus_id;}void focus(U32 id)override{focus_id=id;}
    std::array<std::int32_t,2> cursor()const override{return mouse;}void cursor(std::int32_t x,std::int32_t y)override{mouse={x,y};}
    int show_cursor(bool show)override{return show?++cursor_count:--cursor_count;}std::uint16_t key(U32 vk)override{if(vk>=keys.size())return 0;const auto result=keys[vk];keys[vk]&=0x8000;return result;}
    std::vector<w::Event> events(unsigned)override{auto result=std::move(pending);pending.clear();for(const auto& event:result)if(event.client_cursor)mouse=*event.client_cursor;return result;}
    U32 message_box(std::u16string_view,std::u16string_view,U32)override{throw std::runtime_error("unexpected native message box");}
    void create_dialog(const w::DialogState& s)override{dialogs.emplace(s.token,s);}
    void update_dialog(const w::DialogState& s)override{dialogs.at(s.token)=s;}
    void menu(U32 id,const w::MenuTemplate& value)override{if(!popups.contains(id)){++menus;last_menu=value;}}
};
int main(int argc,char** argv){try{
    CHECK(argc==2);auto host=std::make_shared<MemoryHost>();wr::ProcessBackends backends;
    backends.gui=[host](wr::Process& p){return w::install(p,host);};
    backends.directdraw=[host](wr::Process& p){return wa2::platform::compat::install_legacy_display(p,host);};
    wa2::aot::Runtime runtime({},std::move(backends),host);auto& p=runtime.process();const auto image=wa2::aot::initial_image();
    // Production COM implementations are bound before load, without SDL or
    // a device. Present and display/cursor calls cannot become leaf services.
    for(const char* name:{"COM.2.SetRenderState","COM.2.SetTextureStageState","COM.2.SetSamplerState","COM.2.DrawPrimitiveUP","COM.2.Present"})CHECK(p.resolve("wa2.d3d9",name));
    for(const auto& item:image.imports){const auto name=item.ordinal?"#"+std::to_string(*item.ordinal):item.name;if(!p.resolve(item.dll,name))throw std::runtime_error("missing full-host import: "+item.dll+"!"+name);}
    runtime.load(image);auto& m=p.memory;
    const auto graphics_base=p.cpu;unsigned certified_graphics_checks{};
    for(const char* name:{"COM.2.SetRenderState","COM.2.SetTextureStageState","COM.2.SetSamplerState","COM.2.DrawPrimitiveUP"}){
        p.cpu=graphics_base;p.cpu.r[wr::ESP]-=64;p.cpu.eip=p.resolve("wa2.d3d9",name);
        for(unsigned n=0;n<6;++n)m.store(p.cpu.r[wr::ESP]+n*4,0,32);
        const auto before=p.cpu;bool rejected{};try{p.dispatch_leaf_api();}catch(const wr::GuestFault& fault){rejected=fault.kind==wr::FaultKind::unsupported && std::string(fault.what()).find("stale guest COM object")!=std::string::npos;}
        CHECK(rejected && p.cpu.r==before.r && p.cpu.eip==before.eip);++certified_graphics_checks;
    }
    for(const char* name:{"COM.2.Present","COM.2.GetDisplayMode","COM.2.SetCursorPosition","COM.2.Clear"}){
        p.cpu=graphics_base;p.cpu.eip=p.resolve("wa2.d3d9",name);CHECK(!p.dispatch_leaf_api());++certified_graphics_checks;
    }
    p.cpu=graphics_base;CHECK(certified_graphics_checks==8);
    const auto call=[&](const char* name,std::initializer_list<U32> args){const auto target=p.resolve("user32.dll",name);CHECK(target);return p.callback(target,{args.begin(),args.size()});};
    unsigned callbacks{},dialog_init{},commands{},menu_clicks{};
    p.register_api("fixture","window_proc",4,[&](wr::Process& q){++callbacks;const auto id=q.argument(0),message=q.argument(1);if(message==0x111){++commands;return U32(0);}
        if(message==0x201){CHECK(q.argument(2)==1 && q.argument(3)==w::packed_pointer({640,250}));++menu_clicks;return U32(0);}return call("DefWindowProcA",{id,message,q.argument(2),q.argument(3)});});
    p.register_api("fixture","dialog_proc",4,[&](wr::Process& q){if(q.argument(1)==0x110){++dialog_init;CHECK(q.argument(3)==0x12345678);}return U32(0);});
    const auto work=p.allocate_bytes(65536),name=p.put_string("SyntheticMemoryWindow"),title=p.put_string("fixture");
    wr::port::write(p,work,std::array<U32,10>{0,p.resolve("fixture","window_proc"),0,12,p.image_base,0,0,0,0,name});
    const auto atom=call("RegisterClassA",{work});CHECK(atom);
    const auto window=call("CreateWindowExA",{0,atom,title,0x10000000,10,20,1280,720,0,0,p.image_base,0});CHECK(window && host->windows.size()==1 && callbacks>=2);
    unsigned system_messages{};const auto system=p.gui()->attach_system_dialog([&](U32 message,U32 first,U32 second){CHECK(message==0x466 && first==1 && second==title);++system_messages;return U32(7);});
    CHECK(call("SendMessageA",{system,0x466,1,title})==7 && system_messages==1);p.gui()->detach_system_dialog(system);
    CHECK(call("SetWindowLongA",{window,U32(-21),0xfeedcafe})==0);CHECK(call("SetWindowLongA",{window,U32(-21),7})==0xfeedcafe);
    CHECK(call("GetClientRect",{window,work})==1 && m.load(work+8,32)==1280 && m.load(work+12,32)==720);
    CHECK(call("SetCursorPos",{52,84})==1 && host->mouse[0]==42 && host->mouse[1]==64);
    CHECK(call("GetCursorPos",{work})==1 && m.load(work,32)==52 && m.load(work+4,32)==84);
    CHECK(call("ScreenToClient",{window,work})==1 && m.load(work,32)==42 && m.load(work+4,32)==64);
    CHECK(call("ClientToScreen",{window,work})==1 && m.load(work,32)==52 && m.load(work+4,32)==84);
    unsigned pointer_cases{};
    const auto mapping=[&](int width,int height,int dw,int dh){return w::PointerCoordinates{width,height,dw,dh,1280,720,host->content_bounds(dw,dh,1280,720)};};
    for(const auto map:{mapping(1280,720,1280,720),mapping(1280,720,1920,1080),mapping(1280,720,2560,1440)}){
        CHECK((map.source(640,250)==std::array<std::int32_t,2>{640,250}));
        CHECK((map.window(640,250)==std::array<int,2>{640,250}));++pointer_cases;
    }
    CHECK((mapping(2400,1080,2400,1080).source(1200,540)==std::array<std::int32_t,2>{640,360}));++pointer_cases;
    CHECK((mapping(1000,900,1000,900).source(500,450)==std::array<std::int32_t,2>{640,360}));++pointer_cases;
    CHECK((mapping(1000,700,1250,875).source(500,350)==std::array<std::int32_t,2>{640,360}));++pointer_cases;
    CHECK((mapping(1080,2400,1080,2400).source(540,1200)==std::array<std::int32_t,2>{640,360}));++pointer_cases;
    CHECK(mapping(2400,1080,2400,1080).source(239.9,540)->at(0)==-1);
    CHECK(mapping(1280,1000,1280,1000).source(640,139.9)->at(1)==-1);++pointer_cases;
    CHECK(!mapping(0,0,0,0).source(0,0) && !mapping(1280,720,1280,720).source(std::numeric_limits<double>::quiet_NaN(),0));++pointer_cases;
    // Three events in one pump have distinct positions. MSG.pt is in source
    // screen space, whereas button/motion lParam remains in client space.
    for(const auto point:{std::array<std::int32_t,2>{100,200},std::array<std::int32_t,2>{640,250},std::array<std::int32_t,2>{900,500}}){
        w::Event event{w::Event::Kind::message,window,0x200,0,w::packed_pointer(point)};event.client_cursor=point;host->pending.push_back(event);
    }
    CHECK(call("GetCursorPos",{work})==1 && m.load(work,32)==910 && m.load(work+4,32)==520);
    for(const auto point:{std::array<std::int32_t,2>{100,200},std::array<std::int32_t,2>{640,250},std::array<std::int32_t,2>{900,500}}){
        CHECK(call("PeekMessageA",{work,window,0x200,0x200,1})==1 && m.load(work+12,32)==w::packed_pointer(point));
        CHECK(m.load(work+20,32)==U32(point[0]+10) && m.load(work+24,32)==U32(point[1]+20));
        CHECK(call("ScreenToClient",{window,work+20})==1 && m.load(work+20,32)==U32(point[0]) && m.load(work+24,32)==U32(point[1]));++pointer_cases;
    }
    w::Event wheel{w::Event::Kind::message,window,0x20a,120u<<16,w::packed_pointer({-12,250})};wheel.client_cursor={{-12,250}};host->pending.push_back(wheel);
    CHECK(call("PeekMessageA",{work,window,0x20a,0x20a,1})==1 && m.load(work+12,32)==w::packed_pointer({-2,270}));++pointer_cases;
    w::Event click{w::Event::Kind::message,window,0x201,1,w::packed_pointer({640,250})};click.client_cursor={{640,250}};host->pending.push_back(click);
    w::Event after_click{w::Event::Kind::message,window,0x200,0,w::packed_pointer({1000,600})};after_click.client_cursor={{1000,600}};host->pending.push_back(after_click);
    CHECK(call("PeekMessageA",{work,window,0x201,0x201,1})==1 && m.load(work+20,32)==650 && m.load(work+24,32)==270);
    CHECK(call("DispatchMessageA",{work})==0 && menu_clicks==1);++pointer_cases;
    // Execute only the cached GetCursorPos/ScreenToClient prefix used by the
    // game's polling path. Stop before its later game-state calculations.
    {w::Event event{w::Event::Kind::message,window,0x200,0,w::packed_pointer({640,250})};event.client_cursor={{640,250}};host->pending.push_back(event);
        const auto before=p.cpu;const auto stack=p.allocate_bytes(4096);p.cpu.r[wr::ESP]=stack+2048;m.store(p.cpu.r[wr::ESP]+4,window,32);p.cpu.eip=0x44a860;
        wa2::aot::Program program(m);std::uint64_t budget=256;unsigned boundaries{};
        while(p.cpu.eip!=0x44a881){CHECK(++boundaries<=12);if(!program.invoke(p.cpu,budget))CHECK(p.dispatch_api());}
        CHECK(m.load(p.cpu.r[wr::ESP]+20,32)==640 && m.load(p.cpu.r[wr::ESP]+24,32)==250);p.cpu=before;m.release(stack);++pointer_cases;
    }
    unsigned hit_cases{},client_viewport_hit_cases{};
    // Run the complete existing mouse calculator and hit-test leaf, including
    // its aspect-ratio arithmetic and button polling. Only its 128 synthetic
    // rectangles and input globals are supplied; no scene or game entry runs.
    {const auto before=p.cpu;std::vector<std::uint8_t> saved(0xf060);m.copy_out(0xb601a0,saved);const auto stack=p.allocate_bytes(4096);
        const auto run_leaf=[&](U32 width,U32 height,std::array<std::int32_t,2> point,bool down,unsigned index=0,unsigned bank=0,std::optional<std::uint16_t> key={}){
            m.copy_in(0xb601a0,std::vector<std::uint8_t>(0xf060));
            m.store(0xb601e0,1280,16);m.store(0xb601e2,720,16);m.store(0xb601e4,width,16);m.store(0xb601e6,height,16);
            m.store(0xb601dc,bank,8);const auto rectangle=0xb60200+bank*1280+index*10;
            m.store(rectangle,1,8);m.store(rectangle+2,480,16);m.store(rectangle+4,220,16);m.store(rectangle+6,320,16);m.store(rectangle+8,60,16);
            host->pending.clear();host->mouse=point;host->keys.fill(0);host->keys[1]=key.value_or(down?0x8001:0);
            p.cpu=before;p.cpu.r[wr::ESP]=stack+2048;wr::port::write(p,p.cpu.r[wr::ESP],std::array<U32,4>{0xf0001234,window,1,0});p.cpu.eip=0x44a860;
            wa2::aot::Program program(m);std::uint64_t budget=32768;unsigned boundaries{};
            while(p.cpu.eip!=0xf0001234){CHECK(++boundaries<32);if(!program.invoke(p.cpu,budget))CHECK(p.dispatch_api());}
            const auto x=std::int32_t(m.load(0xb601a0,32)),y=std::int32_t(m.load(0xb601a4,32));
            if(x!=640 || y!=250 || m.load(0xb601d4,16)!=index || m.load(0xb601aa,8)!=U32(down))throw std::runtime_error("full mouse leaf: client="+std::to_string(width)+"x"+std::to_string(height)+" point="+std::to_string(point[0])+","+std::to_string(point[1])+" result="+std::to_string(x)+","+std::to_string(y)+" hit="+std::to_string(m.load(0xb601d4,16))+" down="+std::to_string(m.load(0xb601aa,8)));
            ++hit_cases;
        };
        for(bool down:{false,true}){run_leaf(1280,720,{640,250},down);run_leaf(1920,1080,{960,375},down);run_leaf(1600,900,{800,313},down);run_leaf(1920,1200,{960,435},down);run_leaf(1280,1024,{640,402},down);run_leaf(1600,720,{800,250},down);}
        // A visible button at (640,250) in a 1280x720 source window can be
        // rendered through a larger backbuffer. Feed the actual adapter's
        // result into the complete source calculator, rather than assuming
        // that backbuffer coordinates are already source-client coordinates.
        for(const int density:{1,2}){
            w::PointerCoordinates map{1280,720,1280*density,720*density,1920,1080,host->content_bounds(1280*density,720*density,1920,1080)};
            map.client_content=host->content_bounds(1280,720,1920,1080);
            run_leaf(1280,720,*map.source(640,250),true);
            CHECK((map.window(640,250)==std::array<int,2>{640,250}));
        }
        {w::PointerCoordinates map{1920,1200,1920,1200,1280,720,host->content_bounds(1920,1200,1280,720)};
            map.client_content=host->content_bounds(1920,1200,1280,720);run_leaf(1920,1200,*map.source(960,435),true);CHECK((map.window(960,435)==std::array<int,2>{960,435}));}
        {w::PointerCoordinates map{2400,1080,2400,1080,1920,1080,host->content_bounds(2400,1080,1920,1080)};
            map.client_content=host->content_bounds(1280,720,1920,1080);run_leaf(1280,720,*map.source(1200,375),true);CHECK((map.window(640,250)==std::array<int,2>{1200,375}));}
        {w::PointerCoordinates map{1280,720,1280,720,640,360,host->content_bounds(1280,720,640,360)};
            map.client_content=host->content_bounds(1280,720,640,360);run_leaf(1280,720,*map.source(640,250),true);}
        // The actual adapter now uses the same client viewport as Present,
        // independently of cropped or phone-sized render targets.
        for(const int density:{1,2}){
            const auto viewport=host->client_viewport(window,2400*density,1080*density);
            const auto map=w::PointerCoordinates::from_client(2400,1080,2400*density,1080*density,viewport);
            run_leaf(1280,720,*map.source(1200,375),true);++client_viewport_hit_cases;
        }
        for(const auto size:{std::array<int,2>{1920,1200},std::array<int,2>{2400,1080}}){
            const wa2::graphics::ClientViewport viewport{size,host->content_bounds(size[0],size[1],size[0],size[1])};
            const auto map=w::PointerCoordinates::from_client(size[0],size[1],size[0],size[1],viewport);
            run_leaf(size[0],size[1],*map.source(size[0]/2,size[1]==1200?435:375),true);++client_viewport_hit_cases;
        }
        run_leaf(1280,720,{640,250},true,17,3);run_leaf(1280,720,{640,250},true,127,47);run_leaf(1280,720,{640,250},true,17,3,1);
        const auto diagnostic=p.state().reports.at("source_pointer")();CHECK(diagnostic.find("\"selected_rectangle\":17")!=std::string::npos && diagnostic.find("[17,480,220,320,60]")!=std::string::npos);
        p.cpu=before;m.copy_in(0xb601a0,saved);m.release(stack);host->keys.fill(0);
    }
    CHECK(call("PostMessageA",{window,0x111,17,0})==1);
    CHECK(call("PeekMessageA",{work,window,0x111,0x111,0})==1 && m.load(work+8,32)==17);
    CHECK(call("PeekMessageA",{work,window,0x111,0x111,1})==1 && call("DispatchMessageA",{work})==0 && commands==1);
    CHECK(call("PeekMessageA",{work,window,0x111,0x111,1})==0);
    const auto menu=call("LoadMenuA",{p.image_base,104});CHECK(menu && call("SetMenu",{window,menu})==1 && call("GetMenu",{window})==menu && host->menus>0);
    CHECK(call("GetSystemMetrics",{54})==29 && call("GetSystemMetrics",{55})==27);
    CHECK(call("GetSystemMetrics",{0})==1920 && call("GetSystemMetrics",{1})==1080);
    bool rejected_metric{};try{call("GetSystemMetrics",{0x7fffffff});}catch(const wr::GuestFault& e){rejected_metric=e.kind==wr::FaultKind::unsupported;}CHECK(rejected_metric);
    CHECK(p.read_string(0x4a3c18)=="menuWnd");unsigned startup_popup_cases{};
    // Replay ONLY the reported finite window-creation prefix, using synthetic
    // window callbacks. No game entry, native window or event loop runs.
    for(const bool decorated:{true,false}){
        host->native_metrics=decorated;CHECK(call("GetSystemMetrics",{55})==(decorated?27u:0u));
        const auto before=p.cpu;m.store(0x4c2220,p.image_base,32);m.store(0x4c2224,window,32);m.store(0x4c2218,name,32);
        p.cpu.eip=0x4322a8;p.cpu.r[wr::EAX]=menu;p.cpu.r[wr::EBP]=0;
        p.cpu.r[wr::ESI]=p.resolve("user32.dll","GetSystemMetrics");p.cpu.r[wr::EDI]=p.resolve("user32.dll","CreateWindowExA");
        wa2::aot::Program program(m);std::uint64_t budget=512;unsigned boundaries{};
        while(p.cpu.eip!=0x4322db){CHECK(++boundaries<=16);if(!program.invoke(p.cpu,budget))CHECK(p.dispatch_api());}
        const auto popup=p.cpu.r[wr::EAX];CHECK(popup && popup!=window && p.cpu.r[wr::ESP]==before.r[wr::ESP]);p.cpu=before;
        CHECK(host->popups.contains(popup) && host->windows.size()==1 && !call("IsWindowVisible",{popup}));
        const auto& spec=host->popups.get(popup);CHECK(spec.parent==window && spec.title==u"menuWnd" && spec.bounds.width==1920 && spec.bounds.height==(decorated?27:0));
        CHECK(call("GetMenu",{popup})==menu && call("GetClientRect",{popup,work})==1 && m.load(work+12,32)==(decorated?27u:0u));
        CHECK(call("SetWindowPos",{popup,0,0,0,0,decorated?27u:0u,0x14})==1);
        CHECK(call("GetClientRect",{popup,work})==1 && m.load(work+8,32)==0);
        CHECK(call("ShowWindow",{popup,0})==0 && call("DestroyWindow",{popup})==1 && !host->popups.contains(popup));
        CHECK(host->windows.size()==1 && call("GetClientRect",{window,work})==1 && m.load(work+8,32)==1280 && m.load(work+12,32)==720);++startup_popup_cases;
    }
    host->native_metrics=true;
    CHECK(call("EnableMenuItem",{menu,0,0x403})!=0xffffffffu);CHECK(call("CheckMenuItem",{menu,0,0x408})!=0xffffffffu);
    CHECK(call("DrawMenuBar",{window})==1);const auto menu_baseline=host->menus;
    CHECK(!host->last_menu.items.empty() && (host->last_menu.items[0].state&0xb)==0xb);
    for(unsigned n=0;n<128;++n){
        CHECK(call("EnableMenuItem",{menu,0,0x403})==3);CHECK(call("CheckMenuItem",{menu,0,0x408})==8);
        CHECK(call("SetMenu",{window,menu})==1);CHECK(call("DrawMenuBar",{window})==1);
    }
    CHECK(host->menus==menu_baseline);
    // A typical radio/check refresh clears items before rechecking the chosen
    // one. Native presentation must see only the completed menu model.
    CHECK(call("CheckMenuItem",{menu,0,0x400})==8);CHECK(call("CheckMenuItem",{menu,0,0x408})==0);
    CHECK(call("EnableMenuItem",{menu,0,0x400})==3);CHECK(call("EnableMenuItem",{menu,0,0x403})==0);
    CHECK(call("GetCursorPos",{work})==1 && host->menus==menu_baseline);
    CHECK(call("CheckMenuItem",{menu,0,0x400})==8);CHECK(call("EnableMenuItem",{menu,0,0x400})==3);
    CHECK(host->menus==menu_baseline);CHECK(call("GetCursorPos",{work})==1);
    CHECK(host->menus==menu_baseline+1 && (host->last_menu.items[0].state&0xb)==0);
    unsigned slider_checks{},button_checks{},dialog_visibility_cases{},hidden_dialogs{};
    for(const U32 resource:{104u,108u,109u,112u,113u,114u,115u}){
    const auto d=call("CreateDialogParamA",{p.image_base,resource,window,p.resolve("fixture","dialog_proc"),0x12345678});CHECK(d);
    const bool initial_visible=(host->dialogs.at(d).definition.style&0x10000000u)!=0;
    CHECK(host->dialogs.at(d).visible==initial_visible);hidden_dialogs+=!initial_visible;
    CHECK(call("ShowWindow",{d,5})==U32(initial_visible));CHECK(call("IsWindowVisible",{d})==1);
    const auto controls=host->dialogs.at(d).controls;for(const auto& state:controls){const auto id=call("GetDlgItem",{d,state.definition.id});CHECK(id);
        if(const auto* type=std::get_if<std::u16string>(&state.definition.type);type && *type==u"msctls_trackbar32"){
            CHECK(call("SendMessageA",{id,0x406,1,100u<<16})==0);CHECK(call("SendMessageA",{id,0x405,1,73})==0);CHECK(call("SendMessageA",{id,0x400,0,0})==73);++slider_checks;}
        if(const auto* type=std::get_if<std::uint16_t>(&state.definition.type);type && *type==128){CHECK(call("SendMessageA",{id,0xf1,1,0})==0);CHECK(call("SendMessageA",{id,0xf0,0,0})==1);++button_checks;}}
    CHECK(host->dialogs.at(d).visible);CHECK(call("ShowWindow",{d,0})==1 && !host->dialogs.at(d).visible);++dialog_visibility_cases;
    CHECK(call("EndDialog",{d,1})==1);CHECK(call("DestroyWindow",{d})==1 && !host->dialogs.contains(d));}
    CHECK(slider_checks==8 && button_checks==24 && dialog_init==7);
    CHECK(hidden_dialogs>0);
    p.register_api("fixture","modal_dialog_proc",4,[&](wr::Process& q){if(q.argument(1)==0x110){CHECK(host->dialogs.at(q.argument(0)).visible);CHECK(call("EndDialog",{q.argument(0),42})==1);++dialog_visibility_cases;}return U32(0);});
    CHECK(call("DialogBoxParamA",{p.image_base,104,window,p.resolve("fixture","modal_dialog_proc"),0})==42);
    // Assembling an original effect and using its source COM byte buffer must
    // not initialize SDL, a GL context or an audio device.
    const std::string shader_text="ps_3_0\ndcl_color0 v0\nmov oC0, v0\n";const auto source=p.put_string(shader_text);
    CHECK(p.callback(p.resolve("d3dx9_41.dll","D3DXAssembleShader"),std::array<U32,7>{source,U32(shader_text.size()),0,0,0,work,work+4})==0);
    const auto buffer=m.load(work,32);CHECK(buffer);const auto method=[&](unsigned slot){return p.callback(m.load(m.load(buffer,32)+slot*4,32),std::array<U32,1>{buffer});};
    CHECK(method(4)>4 && method(3));CHECK(method(2)==0);
    const wa2::graphics::abi::GUID draw7{0x15e65ec0,0x3b9c,0x11d2,{0xb9,0x2f,0,0x60,0x97,0x97,0xea,0x5b}};
    wr::port::write(p,work+32,draw7);CHECK(p.callback(p.resolve("ddraw.dll","DirectDrawCreateEx"),std::array<U32,4>{0,work,work+32,0})==0);
    const auto display=m.load(work,32);m.store(work+64,380,32);
    CHECK(p.callback(m.load(m.load(display,32)+11*4,32),std::array<U32,3>{display,work+64,0})==0 && m.load(work+64+60,32)==128u*1024*1024);
    CHECK(p.callback(m.load(m.load(display,32)+2*4,32),std::array<U32,1>{display})==0);
    CHECK(call("DestroyWindow",{window})==1 && host->windows.empty());
    CHECK(call("PostQuitMessage",{7})==0);CHECK(call("GetMessageA",{work,0,0x12,0x12})==0 && m.load(work+8,32)==7);
    CHECK(host->frames==0 && p.executed_instructions==0);
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.shared-window-model.v1\",\"source_callbacks\":"<<callbacks<<",\"pointer_cases\":"<<pointer_cases<<",\"full_mouse_leaf_cases\":"<<hit_cases<<",\"client_viewport_hit_cases\":"<<client_viewport_hit_cases<<",\"dialog_visibility_cases\":"<<dialog_visibility_cases<<",\"initially_hidden_modeless_dialogs\":"<<hidden_dialogs<<",\"backbuffer_to_client_hit_test\":true,\"hit_table_banks_and_indices\":true,\"transient_left_click\":true,\"source_pointer_diagnostics\":true,\"dpi_resize_and_letterbox_mapping\":true,\"per_event_screen_cursor\":true,\"cursor_poll_refresh\":true,\"cached_cursor_poll_prefix\":true,\"dialog_controls\":true,\"message_filter_remove_dispatch\":true,\"menu_updates\":true,\"redundant_menu_calls_checked\":512,\"redundant_native_menu_updates\":0,\"menu_state_changes_coalesced\":true,\"source_pointer_layouts\":true,\"system_metrics_54_55\":true,\"startup_hidden_popup_cases\":"<<startup_popup_cases<<",\"zero_size_popup_preserved\":true,\"unknown_metric_rejected\":true,\"effect_assembly_and_buffer_release\":true,\"native_window_created\":false,\"graphics_device_created\":false,\"audio_device_opened\":false,\"screen_access\":false,\"game_launched\":false,\"passed\":true}\n";
    std::cout<<"PASS common window model, 512 redundant menu calls without native updates, coalesced state changes and effect buffers in synthetic memory.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
