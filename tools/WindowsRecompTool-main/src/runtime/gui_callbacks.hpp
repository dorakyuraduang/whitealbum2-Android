struct Frame {
    Gui& g;Process& p;std::shared_ptr<Window> w;UINT message;WPARAM native_w;LPARAM native_l;U32 wp{},lp{};
    Scratch scratch;std::function<void()> to_native=[]{},to_guest=[]{};std::vector<U32> transient;
    Frame(Gui& gui,std::shared_ptr<Window> window,UINT m,WPARAM a,LPARAM b):g(gui),p(gui.p),w(std::move(window)),message(m),native_w(a),native_l(b),scratch(p){
        try{marshal();}catch(const GuestFault&){throw;}catch(const std::exception& e){g.unsupported("window message "+hex(m)+" wParam="+hex(a)+" lParam="+hex(std::uintptr_t(b))+": "+e.what());}
        g.frames.push_back(this);
    }
    ~Frame(){if(!g.frames.empty() && g.frames.back()==this)g.frames.pop_back();for(auto id:transient)g.handles.erase(id);}
    U32 borrow(Kind kind,std::uintptr_t native){if(!native)return 0;const auto id=g.token(kind,native,0,false,false);transient.push_back(id);return id;}
    void plain(void* native,std::size_t n){if(!native)g.unsupported("null native structure in window callback");lp=scratch.alloc(n);auto* b=static_cast<std::uint8_t*>(native);to_guest=[this,b,n]{p.memory.copy_in(lp,std::span(b,n));};to_native=[this,b,n]{auto v=read_bytes(p,lp,n);std::copy(v.begin(),v.end(),b);};to_guest();}
    void position(WINDOWPOS* host,U32 at) {
        p.memory.store(at,g.window_token(host->hwnd),32);p.memory.store(at+4,g.ztoken(host->hwndInsertAfter),32);
        const std::array<U32,5> values{U32(host->x),U32(host->y),U32(host->cx),U32(host->cy),host->flags};for(unsigned i=0;i<5;++i)p.memory.store(at+8+4*i,values[i],32);
    }
    void unposition(WINDOWPOS* host,U32 at){if(g.message_window(p.memory.load(at,32))!=host->hwnd)g.unsupported("WINDOWPOS target replacement");host->hwndInsertAfter=g.zorder(p.memory.load(at+4,32));host->x=signed32(p.memory.load(at+8,32));host->y=signed32(p.memory.load(at+12,32));host->cx=signed32(p.memory.load(at+16,32));host->cy=signed32(p.memory.load(at+20,32));host->flags=p.memory.load(at+24,32);}
    void marshal(){
        static_assert(sizeof(RECT)==16 && sizeof(MINMAXINFO)==40 && sizeof(STYLESTRUCT)==8);
        switch(message){
        case WM_NCCREATE:case WM_CREATE:{
            auto* cs=reinterpret_cast<CREATESTRUCTA*>(native_l);if(!cs || cs->lpCreateParams!=w.get())g.unsupported("unexpected native creation payload");
            lp=scratch.alloc(48);wp=scalar(native_w);const U32 title=scratch.string(w->title),name=w->create[1]<65536?w->create[1]:scratch.string(w->klass->logical);
            const std::array<U32,12> fields{w->create[11],w->create[10],w->create[9],w->create[8],U32(cs->cy),U32(cs->cx),U32(cs->y),U32(cs->x),U32(cs->style),title,name,cs->dwExStyle};
            for(unsigned n=0;n<fields.size();++n)p.memory.store(lp+4*n,fields[n],32);
            // CREATESTRUCT is an input notification. The native context pointer,
            // module and class registration must not be replaced by guest values.
            to_native=[this,fields]{for(unsigned n:{0u,1u,2u,3u,9u,10u})if(p.memory.load(lp+4*n,32)!=fields[n])g.unsupported("CREATESTRUCT pointer/handle replacement is not supported");};break;}
        case WM_GETMINMAXINFO:wp=scalar(native_w);plain(reinterpret_cast<void*>(native_l),40);break;
        case WM_WINDOWPOSCHANGING:case WM_WINDOWPOSCHANGED:{
            auto* pos=reinterpret_cast<WINDOWPOS*>(native_l);if(!pos)g.unsupported("null WINDOWPOS");wp=scalar(native_w);lp=scratch.alloc(28);
            to_guest=[this,pos]{position(pos,lp);};to_native=[this,pos]{unposition(pos,lp);};to_guest();break;}
        case WM_NCCALCSIZE:{
            wp=scalar(native_w);if(!wp){plain(reinterpret_cast<void*>(native_l),16);break;}
            auto* cs=reinterpret_cast<NCCALCSIZE_PARAMS*>(native_l);if(!cs || !cs->lppos)g.unsupported("null NCCALCSIZE payload");lp=scratch.alloc(52);const U32 pos=scratch.alloc(28);
            to_guest=[this,cs,pos]{p.memory.copy_in(lp,std::span(reinterpret_cast<const std::uint8_t*>(cs->rgrc),48));p.memory.store(lp+48,pos,32);position(cs->lppos,pos);};
            to_native=[this,cs,pos]{if(p.memory.load(lp+48,32)!=pos)g.unsupported("NCCALCSIZE pointer replacement");auto bytes=read_bytes(p,lp,48);std::copy(bytes.begin(),bytes.end(),reinterpret_cast<std::uint8_t*>(cs->rgrc));unposition(cs->lppos,pos);};to_guest();break;}
        case WM_STYLECHANGING:case WM_STYLECHANGED:wp=scalar(native_w);plain(reinterpret_cast<void*>(native_l),8);break;
        case WM_SIZING:case WM_MOVING:wp=scalar(native_w);plain(reinterpret_cast<void*>(native_l),16);break;
        case WM_SETTEXT:{wp=scalar(native_w);const auto* text=reinterpret_cast<const char*>(native_l);if(!text){lp=0;break;}std::size_t n=0;while(n<32768 && text[n])++n;if(n==32768)g.unsupported("oversized native window text");lp=scratch.string(std::string(text,n));break;}
        case WM_GETTEXT:{wp=scalar(native_w);if(wp>32768)g.unsupported("oversized native text output");if(!native_l && wp)g.unsupported("null native text output");lp=wp?scratch.alloc(wp):0;break;}
        case WM_MOUSEACTIVATE:case WM_SETCURSOR:case WM_SETFOCUS:case WM_KILLFOCUS:case WM_CONTEXTMENU:wp=g.window_token(reinterpret_cast<HWND>(native_w));lp=scalar(std::uintptr_t(native_l));break;
        case WM_ACTIVATE:case WM_CAPTURECHANGED:wp=scalar(native_w);lp=g.window_token(reinterpret_cast<HWND>(native_l));break;
        case WM_INPUTLANGCHANGE:case WM_INPUTLANGCHANGEREQUEST:wp=scalar(native_w);lp=g.token(Kind::keyboard_layout,std::uintptr_t(native_l));break;
        case WM_ACTIVATEAPP:wp=scalar(native_w);lp=U32(native_l)==GetCurrentThreadId()?1u:scalar(std::uintptr_t(native_l));break;
        case WM_NCACTIVATE:wp=scalar(native_w);lp=native_l==-1?0xffffffffu:g.window_token(reinterpret_cast<HWND>(native_l));break;
        case WM_ERASEBKGND:wp=borrow(Kind::borrowed_dc,std::uintptr_t(native_w));lp=scalar(std::uintptr_t(native_l));break;
        case WM_NCPAINT:wp=native_w<=1?U32(native_w):borrow(Kind::region,std::uintptr_t(native_w));lp=scalar(std::uintptr_t(native_l));break;
        case WM_SETICON:wp=scalar(native_w);lp=g.token(Kind::icon,std::uintptr_t(native_l));break;
        case WM_GETICON:wp=scalar(native_w);lp=scalar(std::uintptr_t(native_l));break;
        case WM_COMMAND:wp=scalar(native_w);lp=g.window_token(reinterpret_cast<HWND>(native_l));break;
        case WM_HSCROLL:case WM_VSCROLL:wp=scalar(native_w);lp=g.window_token(reinterpret_cast<HWND>(native_l));break;
        case WM_CTLCOLORMSGBOX:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:case WM_CTLCOLORDLG:case WM_CTLCOLORSCROLLBAR:case WM_CTLCOLORSTATIC:
            wp=borrow(Kind::borrowed_dc,std::uintptr_t(native_w));lp=g.window_token(reinterpret_cast<HWND>(native_l));break;
        case WM_SETFONT:wp=g.token(Kind::gdi,std::uintptr_t(native_w));lp=scalar(std::uintptr_t(native_l));break;
        case WM_GETDLGCODE:{
            wp=scalar(native_w);if(!native_l){lp=0;break;}const auto* msg=reinterpret_cast<const MSG*>(native_l);lp=scratch.alloc(28);g.write_message(lp,*msg);break;}
        case WM_NOTIFY:{
            wp=scalar(native_w);auto* header=reinterpret_cast<NMHDR*>(native_l);if(!header)g.unsupported("null NMHDR");const auto code=header->code;
            const unsigned extra=code==UDN_DELTAPOS || code==TRBN_THUMBPOSCHANGING?8u:0u;
            if(code!=UDN_DELTAPOS && code!=TRBN_THUMBPOSCHANGING && code!=TCN_SELCHANGE && code!=TCN_SELCHANGING && code!=NM_RELEASEDCAPTURE && code!=NM_CLICK && code!=NM_DBLCLK && code!=NM_RETURN && code!=NM_SETFOCUS && code!=NM_KILLFOCUS)g.unsupported("WM_NOTIFY requires a typed native payload: "+hex(code));
            lp=scratch.alloc(12+extra);p.memory.store(lp,g.window_token(header->hwndFrom),32);p.memory.store(lp+4,scalar(header->idFrom),32);p.memory.store(lp+8,code,32);
            if(extra){auto* data=reinterpret_cast<std::uint8_t*>(header)+sizeof(NMHDR);p.memory.copy_in(lp+12,std::span(data,extra));to_native=[this,data,extra]{auto bytes=read_bytes(p,lp+12,extra);std::copy(bytes.begin(),bytes.end(),data);};}break;}
        case WM_DEVICECHANGE:{
            wp=scalar(native_w);if(!native_l){lp=0;break;}
            const auto* header=reinterpret_cast<const DEV_BROADCAST_HDR*>(native_l);
            check_device_broadcast_size(header->dbch_size,header->dbch_devicetype);
            const auto bytes=std::span(reinterpret_cast<const std::uint8_t*>(header),header->dbch_size);check_device_broadcast(bytes);
            lp=scratch.alloc(bytes.size());p.memory.copy_in(lp,bytes);
            // Notification data is input. DefWindowProc uses the original
            // native payload via this Frame; guest addresses never escape.
            break;}
        case WM_PARENTNOTIFY:wp=scalar(native_w);lp=(LOWORD(native_w)==WM_CREATE || LOWORD(native_w)==WM_DESTROY)?g.window_token(reinterpret_cast<HWND>(native_l)):scalar(std::uintptr_t(native_l));break;
        case WM_INITMENU:case WM_INITMENUPOPUP:case WM_UNINITMENUPOPUP:wp=g.token(Kind::menu,std::uintptr_t(native_w));lp=scalar(std::uintptr_t(native_l));break;
        case WM_MENUSELECT:case WM_MENUCHAR:wp=scalar(native_w);lp=g.token(Kind::menu,std::uintptr_t(native_l));break;
        case WM_ENTERIDLE:wp=scalar(native_w);lp=g.window_token(reinterpret_cast<HWND>(native_l));break;
        default:
            if(!scalar_message(message) || (message==WM_TIMER && native_l))g.unsupported("unmarshalled native window message "+hex(message));
            wp=scalar(native_w);lp=scalar(std::uintptr_t(native_l));break;
        }
    }
};
LRESULT CALLBACK Gui::procedure(HWND hwnd,UINT message,WPARAM a,LPARAM b) noexcept {
    std::shared_ptr<Window> window;
    try {
        if(auto it=window_owners.find(hwnd);it!=window_owners.end())window=it->second.lock();
        if(!window && !creating.empty()){
            window=creating.back();window->native=hwnd;window->alive=true;window_owners[hwnd]=window;
            auto& h=window->gui->handles.at(window->id);h.native=reinterpret_cast<std::uintptr_t>(hwnd);
        }
        if(!window)return DefWindowProcA(hwnd,message,a,b);
        auto& g=*window->gui;
        auto forget=[&]{if(message==WM_NCDESTROY){window_owners.erase(hwnd);window->alive=false;if(window->counted)++g.destroyed;}};
        if(g.stopping || g.failure || g.p.exited()){
            const auto value=(g.failure && message==WM_NCCREATE)?0:(g.failure && message==WM_CREATE)?-1:DefWindowProcA(hwnd,message,a,b);forget();return value;
        }
        // Post-Windows-2000 host non-client/accessibility transport is handled
        // by the host. Its undocumented native pointers never enter the guest.
        if(g.host_message(message)){
            ++g.host_only;g.host_only_ids.insert(message);return DefWindowProcA(hwnd,message,a,b);
        }
        if(message==WM_NCCALCSIZE)g.trace_layout(*window,"native-input",a,b);
        Frame frame(g,window,message,a,b);++g.callbacks;if(message==WM_MOUSEACTIVATE)++g.mouse_activations;
        const std::array<U32,4> args{window->id,message,frame.wp,frame.lp};const auto result=g.p.callback(window->procedure,args);
        frame.to_native();
        if(message==WM_NCCALCSIZE)g.trace_layout(*window,"guest-output",a,b,result);
        if(message==WM_GETTEXT && frame.wp){const auto size=std::min<std::size_t>(std::uint64_t(result)+1,frame.wp);auto text=read_bytes(g.p,frame.lp,size);std::copy(text.begin(),text.end(),reinterpret_cast<std::uint8_t*>(b));}
        LRESULT returned=signed_param(result);
        if(message==WM_GETICON || message==WM_SETICON)returned=reinterpret_cast<LRESULT>(g.get<HICON>(result,Kind::icon,true));
        forget();return returned;
    }catch(...){
        if(window){auto& g=*window->gui;if(!g.failure)g.failure=std::current_exception();if(message==WM_NCDESTROY){window_owners.erase(hwnd);window->alive=false;}}
        // Exceptions are saved and rethrown at the calling API boundary, never
        // allowed to cross Windows' WNDPROC ABI or disappear as fake success.
        return message==WM_CREATE?-1:0;
    }
}
