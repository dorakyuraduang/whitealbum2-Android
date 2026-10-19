U32 Gui::register_class(Args a){
    const U32 at=a[0];p.memory.check(at,48,Memory::Read);
    std::array<U32,12> v{};for(unsigned i=0;i<12;++i)v[i]=p.memory.load(at+4*i,32);
    if(v[0]!=48){p.set_error(87);return 0;}if(v[5]!=0 && v[5]!=p.image_base)unsupported("foreign module class registration");
    if(v[3])unsupported("class extra bytes are not implemented");
    if(v[4]>32768 || !v[2] || v[10]<65536){p.set_error(87);return 0;}p.memory.check(v[2],1,Memory::Execute);
    const auto name=p.read_string(v[10],256);if(name.empty()){p.set_error(87);return 0;}
    if(classes.contains(name)){p.set_error(1410);return 0;}
    auto c=std::make_shared<WindowClass>();c->logical=name;c->native="WinRecomp."+std::to_string(GetCurrentProcessId())+"."+std::to_string(reinterpret_cast<std::uintptr_t>(this))+"."+name;
    c->procedure=v[2];c->instance=v[5];c->style=v[1];c->extra=v[4];c->menu_resource=v[9];
    WNDCLASSEXA wc{};wc.cbSize=sizeof(wc);wc.style=v[1];wc.lpfnWndProc=procedure;wc.cbWndExtra=0;wc.hInstance=GetModuleHandleW(nullptr);
    wc.hIcon=get<HICON>(v[6],Kind::icon,true);wc.hCursor=get<HCURSOR>(v[7],Kind::cursor,true);wc.hbrBackground=brush(v[8]);wc.lpszClassName=c->native.c_str();wc.hIconSm=get<HICON>(v[11],Kind::icon,true);
    c->atom=RegisterClassExA(&wc);if(!c->atom){p.set_error(GetLastError());return 0;}classes.emplace(name,c);return c->atom;
}
U32 Gui::create_window(Args a){
    if(a[10]!=0 && a[10]!=p.image_base)unsupported("foreign module window creation");
    if(a[3]&WS_CHILD)unsupported("child/control windows are not implemented");
    auto c=klass(a[1]);auto w=std::make_shared<Window>();w->gui=this;w->klass=c;w->procedure=c->procedure;w->extra.resize(c->extra);std::copy(a.begin(),a.end(),w->create.begin());w->title=a[2]?p.read_string(a[2]):"";
    auto parent=hwnd(a[8],true);U32 menu=a[9];if(!menu && c->menu_resource){const std::array<U32,2> args{p.image_base,c->menu_resource};menu=load_menu(args);if(!menu)return 0;}
    auto native_menu=get<HMENU>(menu,Kind::menu,true);w->id=token(Kind::window,1,0,false,false);windows.emplace(w->id,w);creating.push_back(w);
    HWND handle=CreateWindowExA(a[0],c->native.c_str(),w->title.c_str(),a[3],signed32(a[4]),signed32(a[5]),signed32(a[6]),signed32(a[7]),parent,native_menu,GetModuleHandleW(nullptr),w.get());
    const auto error=GetLastError();creating.pop_back();
    if(!handle){windows.erase(w->id);handles.erase(w->id);if(w->native)window_owners.erase(w->native);p.set_error(error);check();return 0;}
    w->native=handle;w->alive=true;w->counted=true;++created;handles.at(w->id).native=reinterpret_cast<std::uintptr_t>(handle);check();return w->id;
}
U32 Gui::load_icon(Args a){
    ResourceName name=a[1]<65536?ResourceName(std::uint16_t(a[1])):ResourceName([&]{auto s=p.read_string(a[1],4096);std::u16string u;for(unsigned char c:s){if(c>=128)unsupported("named non-ASCII icon resource");u+=char16_t(c);}return u;}());
    if(auto it=icons.find({a[0],name});it!=icons.end())return it->second;
    HICON icon{};bool owned=false;
    if(!a[0]){if(a[1]>=65536){p.set_error(87);return 0;}icon=LoadIconA(nullptr,MAKEINTRESOURCEA(a[1]));}
    else {
        if(a[0]!=p.image_base)unsupported("icons from arbitrary guest DLLs are not supported");
        auto group=find_resource(p.source_image(),std::uint16_t(14),name);if(!group){p.set_error(1814);return 0;}
        auto bytes=resource_bytes(p.source_image(),*group);if(bytes.size()<6)unsupported("short group icon resource");
        auto word=[&](unsigned at){return unsigned(bytes[at])|(unsigned(bytes[at+1])<<8);};const auto count=word(4);
        if(word(0) || word(2)!=1 || !count || count>256 || bytes.size()!=6+std::size_t(count)*14)unsupported("invalid group icon directory");
        // Work on a checked copy; no code from the original module is loaded.
        std::vector<std::uint8_t> copied(bytes.begin(),bytes.end());const int cx=GetSystemMetrics(SM_CXICON),cy=GetSystemMetrics(SM_CYICON);
        const auto id=LookupIconIdFromDirectoryEx(copied.data(),TRUE,cx,cy,LR_DEFAULTCOLOR);
        auto item=find_resource(p.source_image(),std::uint16_t(3),std::uint16_t(id),group->language);if(!item){p.set_error(1814);return 0;}
        auto payload=resource_bytes(p.source_image(),*item);std::vector<std::uint8_t> data(payload.begin(),payload.end());
        if(data.empty()){p.set_error(13);return 0;}icon=CreateIconFromResourceEx(data.data(),DWORD(data.size()),TRUE,0x30000,cx,cy,LR_DEFAULTCOLOR);owned=icon!=nullptr;
    }
    if(!icon){p.set_error(GetLastError());return 0;}
    const auto id=token(Kind::icon,reinterpret_cast<std::uintptr_t>(icon),0,owned);icons.emplace(std::pair{a[0],name},id);return id;
}
U32 Gui::default_window(Args a){
    auto h=hwnd(a[0]);
    for(auto it=frames.rbegin();it!=frames.rend();++it){auto& f=**it;if(f.w->id==a[0] && f.message==a[1] && f.wp==a[2] && f.lp==a[3]){
        f.to_native();if(f.message==WM_NCCALCSIZE)trace_layout(*f.w,"default-input",f.native_w,f.native_l);
        const auto result=DefWindowProcA(h,f.message,f.native_w,f.native_l);
        if(f.message==WM_NCCALCSIZE)trace_layout(*f.w,"default-output",f.native_w,f.native_l,result32(result));f.to_guest();
        if(f.message==WM_GETTEXT && f.wp){const auto size=std::min<std::size_t>(std::uint64_t(result32(result))+1,f.wp);p.memory.copy_in(f.lp,std::span(reinterpret_cast<const std::uint8_t*>(f.native_l),size));}
        if(a[1]==WM_GETICON || a[1]==WM_SETICON)return token(Kind::icon,std::uintptr_t(result));return result32(result);
    }}
    if(a[1]==WM_MOUSEACTIVATE)return result32(DefWindowProcA(h,a[1],reinterpret_cast<WPARAM>(message_window(a[2])),signed_param(a[3])));
    if(a[1]==WM_DEVICECHANGE && a[3]){
        p.memory.check(a[3],12,Memory::Read);const auto size=p.memory.load(a[3],32),type=p.memory.load(a[3]+4,32);
        try{check_device_broadcast_size(size,type);auto bytes=read_bytes(p,a[3],size);check_device_broadcast(bytes);
            return result32(DefWindowProcA(h,a[1],a[2],reinterpret_cast<LPARAM>(bytes.data())));
        }catch(const GuestFault&){throw;}catch(const std::runtime_error& e){unsupported(e.what());}
    }
    if(!scalar_payload(a[1],a[3]) || (a[1]==WM_TIMER && a[3]))unsupported("DefWindowProc with an untracked pointer/handle payload");
    return result32(DefWindowProcA(h,a[1],a[2],signed_param(a[3])));
}
U32 Gui::send_message(Args a,bool post){
    const auto h=hwnd(a[0]);const bool mouse_activate=a[1]==WM_MOUSEACTIVATE;
    if(a[1]==WM_DEVICECHANGE && a[3]){
        if(post){p.set_error(ERROR_MESSAGE_SYNC_ONLY);return 0;}
        p.memory.check(a[3],12,Memory::Read);const auto size=p.memory.load(a[3],32),type=p.memory.load(a[3]+4,32);
        try{check_device_broadcast_size(size,type);auto bytes=read_bytes(p,a[3],size);check_device_broadcast(bytes);
            const auto result=SendMessageA(h,a[1],a[2],reinterpret_cast<LPARAM>(bytes.data()));check();return result32(result);
        }catch(const GuestFault&){throw;}catch(const std::runtime_error& e){unsupported(e.what());}
    }
    if(!post && !guest_window(h) && a[1]!=WM_MOUSEACTIVATE)return control_message(h,a[1],a[2],a[3]);
    if(!post && (a[1]==WM_SETTEXT || a[1]==WM_GETTEXT))return control_message(h,a[1],a[2],a[3]);
    if((!mouse_activate && !scalar_payload(a[1],a[3])) || (a[1]==WM_TIMER && a[3]))unsupported("Send/PostMessage requires a supported scalar payload");
    const WPARAM wp=mouse_activate?reinterpret_cast<WPARAM>(message_window(a[2])):a[2];
    if(post)return native_bool(PostMessageA(h,a[1],wp,signed_param(a[3])));
    return result32(SendMessageA(h,a[1],wp,signed_param(a[3])));
}
void Gui::write_message(U32 at,const MSG& m){
    if((m.message!=WM_MOUSEACTIVATE && !scalar_payload(m.message,std::uintptr_t(m.lParam))) || (m.message==WM_TIMER && m.lParam))unsupported("queued message contains unsupported native data: "+hex(m.message));
    // Legacy public MSG32 is 28 bytes. Host-private lPrivate is not exposed.
    U32 wp{},lp{};
    try{wp=m.message==WM_MOUSEACTIVATE?window_token(reinterpret_cast<HWND>(m.wParam)):scalar(m.wParam);lp=scalar(std::uintptr_t(m.lParam));}catch(const std::exception& e){unsupported("queued message "+hex(m.message)+" wParam="+hex(m.wParam)+" lParam="+hex(std::uintptr_t(m.lParam))+": "+e.what());}
    const std::array<U32,7> v{window_token(m.hwnd),m.message,wp,lp,m.time,U32(m.pt.x),U32(m.pt.y)};
    p.memory.check(at,28,Memory::Write);for(unsigned i=0;i<7;++i)p.memory.store(at+4*i,v[i],32);
}
MSG Gui::read_message(U32 at){
    p.memory.check(at,28,Memory::Read);std::array<U32,7> v{};for(unsigned i=0;i<7;++i)v[i]=p.memory.load(at+4*i,32);
    if((v[1]!=WM_MOUSEACTIVATE && !scalar_payload(v[1],v[3])) || (v[1]==WM_TIMER && v[3]))unsupported("guest MSG contains unimplemented pointer semantics");
    MSG m{};m.hwnd=hwnd(v[0],true);m.message=v[1];m.wParam=v[1]==WM_MOUSEACTIVATE?reinterpret_cast<WPARAM>(message_window(v[2])):v[2];m.lParam=signed_param(v[3]);m.time=v[4];m.pt={signed32(v[5]),signed32(v[6])};return m;
}
U32 Gui::peek_message(Args a,bool blocking){
    p.memory.check(a[0],28,Memory::Write);auto filter=a[1]==0xffffffffu?reinterpret_cast<HWND>(-1):hwnd(a[1],true);MSG m{};
    for(;;){
        const auto value=blocking?GetMessageA(&m,filter,a[2],a[3]):PeekMessageA(&m,filter,a[2],a[3],a[4]);
        const auto error=GetLastError();check();if(value==-1){p.set_error(error);return 0xffffffffu;}
        if(!value){if(blocking)write_message(a[0],m);return 0;}
        // The host thread also owns system/codec/SDL helper windows. Their
        // original MSG64 must reach their native WNDPROC, including private
        // messages such as 0x60 and native pointer payloads.
        const bool native_window=m.hwnd && !guest_window(m.hwnd);
        const bool native_timer=!m.hwnd && m.message==WM_TIMER && m.lParam;
        if(!native_window && !native_timer && !host_message(m.message)){write_message(a[0],m);return U32(value);}
        if(!blocking && !(a[4]&PM_REMOVE)){
            // Consume only the helper packet, leaving the next guest message
            // queued for repeated PM_NOREMOVE calls. Recheck the actual packet:
            // USER32 may process sent messages while removing it.
            const auto target=m.hwnd?m.hwnd:reinterpret_cast<HWND>(-1);
            if(!PeekMessageA(&m,target,m.message,m.message,a[4]|PM_REMOVE)){check();continue;}
            check();
            if(m.message==WM_QUIT){PostQuitMessage(signed32(U32(m.wParam)));continue;}
            if(m.hwnd && guest_window(m.hwnd) && !host_message(m.message))unsupported("native queue target changed while removing a helper message");
        }
        ++native_dispatched;native_message_ids.insert(m.message);
        if(m.hwnd){
            std::array<wchar_t,256> name{};const int length=GetClassNameW(m.hwnd,name.data(),int(name.size()));std::array<char,1024> utf8{};
            if(length){const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,name.data(),length,utf8.data(),int(utf8.size()),nullptr,nullptr);if(bytes>0)native_message_classes.emplace(utf8.data(),std::size_t(bytes));}
        }
        TranslateMessage(&m);DispatchMessageA(&m);check();
    }
}
U32 Gui::paint_begin(Args a){
    auto h=hwnd(a[0]);p.memory.check(a[1],64,Memory::Write);const auto key=std::pair{a[0],a[1]};if(paints.contains(key))unsupported("nested BeginPaint at the same guest structure");
    PAINTSTRUCT ps{};auto native=BeginPaint(h,&ps);const auto error=GetLastError();check();if(!native){p.set_error(error);return 0;}
    const auto id=token(Kind::paint_dc,reinterpret_cast<std::uintptr_t>(native),a[0],false,false);paints.emplace(key,Paint{h,ps,id});
    std::array<std::uint8_t,64> blank{};p.memory.copy_in(a[1],blank);p.memory.store(a[1],id,32);p.memory.store(a[1]+4,U32(ps.fErase),32);p.memory.copy_in(a[1]+8,std::span(reinterpret_cast<const std::uint8_t*>(&ps.rcPaint),16));p.memory.store(a[1]+24,U32(ps.fRestore),32);p.memory.store(a[1]+28,U32(ps.fIncUpdate),32);return id;
}
U32 Gui::paint_end(Args a){
    auto it=paints.find({a[0],a[1]});if(it==paints.end()){p.set_error(87);return 0;}p.memory.check(a[1],64,Memory::Read);
    if(p.memory.load(a[1],32)!=it->second.dc){p.set_error(6);return 0;}const auto value=EndPaint(it->second.native,&it->second.ps);handles.erase(it->second.dc);paints.erase(it);return native_bool(value);
}
void Gui::shutdown() noexcept {
    if(stopping)return;stopping=true;
    // Never execute guest callbacks during destruction, particularly after a
    // guest fault. OS objects are unwound before Process destroys guest memory.
    if(GetCurrentThreadId()!=thread)return;
    for(auto& [key,paint]:paints)EndPaint(paint.native,&paint.ps);paints.clear();
    if(current_gl)wglMakeCurrent(nullptr,nullptr);current_gl=current_dc=0;
    for(const auto& [id,h]:handles)if(h.kind==Kind::gl)wglDeleteContext(reinterpret_cast<HGLRC>(h.native));
    for(const auto& [id,h]:handles)if(h.kind==Kind::dc){auto it=windows.find(h.owner);if(it!=windows.end() || !h.owner)for(unsigned n=0;n<h.references;++n)ReleaseDC(it==windows.end()?nullptr:it->second->native,reinterpret_cast<HDC>(h.native));}
    for(auto& [id,w]:windows)if(w->alive){DestroyWindow(w->native);window_owners.erase(w->native);}windows.clear();
    for(const auto& [name,c]:classes)UnregisterClassA(c->native.c_str(),GetModuleHandleW(nullptr));classes.clear();
    for(const auto& [id,h]:handles)if(h.owned){if(h.kind==Kind::icon)DestroyIcon(reinterpret_cast<HICON>(h.native));if(h.kind==Kind::menu)DestroyMenu(reinterpret_cast<HMENU>(h.native));if(h.kind==Kind::accelerator)DestroyAcceleratorTable(reinterpret_cast<HACCEL>(h.native));}handles.clear();
    if(quit_posted){MSG m{};PeekMessageA(&m,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE);}
    if(active_gui==this)active_gui=nullptr;
}
