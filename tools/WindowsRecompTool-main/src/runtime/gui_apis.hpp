void Gui::install(){
    auto user=[&](const char* name,unsigned count,std::function<U32(Args)> f,U32 bad=0){api("user32.dll",name,count,std::move(f),bad);};
    auto gdi=[&](const char* name,unsigned count,std::function<U32(Args)> f){api("gdi32.dll",name,count,std::move(f));};
    auto gl=[&](const char* name,unsigned count,std::function<U32(Args)> f){api("opengl32.dll",name,count,[this,f=std::move(f)](Args a){ensure_gl();return f(a);});};
    auto wgl=[&](const char* name,unsigned count,std::function<U32(Args)> f){api("opengl32.dll",name,count,std::move(f));};
    user("LoadIconA",2,[this](Args a){return load_icon(a);});
    user("LoadCursorA",2,[this](Args a){if(a[0])unsupported("custom cursor resources are not implemented");if(a[1]>=65536){p.set_error(87);return 0u;}auto value=LoadCursorA(nullptr,MAKEINTRESOURCEA(a[1]));if(!value)p.set_error(GetLastError());return token(Kind::cursor,reinterpret_cast<std::uintptr_t>(value));});
    user("SetCursor",1,[this](Args a){return token(Kind::cursor,reinterpret_cast<std::uintptr_t>(SetCursor(get<HCURSOR>(a[0],Kind::cursor,true))));});
    user("DestroyCursor",1,[this](Args a){auto value=DestroyCursor(get<HCURSOR>(a[0],Kind::cursor));if(value)handles.erase(a[0]);return native_bool(value);});
    user("ShowCursor",1,[](Args a){return U32(ShowCursor(a[0]!=0));});
    user("GetSystemMetrics",1,[](Args a){return U32(GetSystemMetrics(signed32(a[0])));});
    user("GetKeyState",1,[](Args a){return U32(std::int32_t(GetKeyState(signed32(a[0]))));});
    user("GetAsyncKeyState",1,[](Args a){return U32(std::int32_t(GetAsyncKeyState(signed32(a[0]))));});
    user("RegisterClassExA",1,[this](Args a){return register_class(a);});
    user("RegisterClassA",1,[this](Args a){auto bytes=read_bytes(p,a[0],40);Scratch scratch(p);const auto at=scratch.alloc(48);p.memory.store(at,48,32);p.memory.copy_in(at+4,bytes);p.memory.store(at+44,0,32);const std::array<U32,1> args{at};return register_class(args);});
    user("SetRect",5,[this](Args a){p.memory.check(a[0],16,Memory::Write);for(unsigned n=0;n<4;++n)p.memory.store(a[0]+4*n,a[n+1],32);return 1u;});
    user("AdjustWindowRect",3,[this](Args a){auto bytes=read_bytes(p,a[0],16);RECT r{};std::memcpy(&r,bytes.data(),16);p.memory.check(a[0],16,Memory::Write);const auto result=::AdjustWindowRect(&r,a[1],a[2]!=0);if(result)p.memory.copy_in(a[0],std::span(reinterpret_cast<const std::uint8_t*>(&r),16));return native_bool(result);});
    user("LoadAcceleratorsA",2,[this](Args a){return load_accelerators(a);});
    user("TranslateAcceleratorA",3,[this](Args a){auto msg=read_message(a[2]);return U32(::TranslateAcceleratorA(hwnd(a[0]),get<HACCEL>(a[1],Kind::accelerator),&msg));});
    user("LoadMenuA",2,[this](Args a){return load_menu(a);});
    user("GetMenu",1,[this](Args a){return token(Kind::menu,reinterpret_cast<std::uintptr_t>(::GetMenu(hwnd(a[0]))));});
    user("SetMenu",2,[this](Args a){return native_bool(::SetMenu(hwnd(a[0]),get<HMENU>(a[1],Kind::menu,true)));});
    user("DrawMenuBar",1,[this](Args a){return native_bool(::DrawMenuBar(hwnd(a[0])));});
    user("DeleteMenu",3,[this](Args a){return native_bool(::DeleteMenu(get<HMENU>(a[0],Kind::menu),a[1],a[2]));});
    user("EnableMenuItem",3,[this](Args a){return U32(::EnableMenuItem(get<HMENU>(a[0],Kind::menu),a[1],a[2]));});
    user("CheckMenuItem",3,[this](Args a){return U32(::CheckMenuItem(get<HMENU>(a[0],Kind::menu),a[1],a[2]));});
    for(bool insert:{false,true})user(insert?"InsertMenuA":"ModifyMenuA",5,[this,insert](Args a){
        if(a[2]&(MF_BITMAP|MF_OWNERDRAW))unsupported("bitmap/owner-drawn menu payload");
        auto text=a[4]?p.read_string(a[4]):std::string();const auto id=(a[2]&MF_POPUP)?reinterpret_cast<UINT_PTR>(get<HMENU>(a[3],Kind::menu)):UINT_PTR(a[3]);
        return native_bool(insert ? ::InsertMenuA(get<HMENU>(a[0],Kind::menu),a[1],a[2],id,a[4]?text.c_str():nullptr) : ::ModifyMenuA(get<HMENU>(a[0],Kind::menu),a[1],a[2],id,a[4]?text.c_str():nullptr));
    });
    user("FindWindowA",2,[this](Args a){if(a[0] && a[0]<65536)unsupported("FindWindow class atom");auto name=a[0]?p.read_string(a[0]):std::string(),title=a[1]?p.read_string(a[1]):std::string();const char* klass=a[0]?name.c_str():nullptr;
        if(auto it=classes.find(name);it!=classes.end())klass=it->second->native.c_str();return window_token(::FindWindowA(klass,a[1]?title.c_str():nullptr));});
    user("IsWindowVisible",1,[this](Args a){return U32(::IsWindowVisible(message_window(a[0])));});
    user("SetForegroundWindow",1,[this](Args a){return U32(::SetForegroundWindow(message_window(a[0])));});
    user("SetCursorPos",2,[this](Args a){return native_bool(::SetCursorPos(signed32(a[0]),signed32(a[1])));});
    user("SetWindowTextA",2,[this](Args a){auto text=p.read_string(a[1]);return native_bool(::SetWindowTextA(hwnd(a[0]),text.c_str()));});
    user("DialogBoxParamA",5,[this](Args a){return dialog_box(a,true);},0xffffffffu);
    user("CreateDialogParamA",5,[this](Args a){return dialog_box(a,false);});
    user("EndDialog",2,[this](Args a){auto it=windows.find(a[0]);if(it==windows.end() || !it->second->dialog)throw InvalidHandle{ERROR_INVALID_WINDOW_HANDLE};return native_bool(::EndDialog(hwnd(a[0]),signed_param(a[1])));});
    user("GetDlgItem",2,[this](Args a){return window_token(::GetDlgItem(hwnd(a[0]),signed32(a[1])));});
    user("SetDlgItemTextA",3,[this](Args a){const auto text=p.read_string(a[2]);return native_bool(::SetDlgItemTextA(hwnd(a[0]),signed32(a[1]),text.c_str()));});
    user("SendDlgItemMessageA",5,[this](Args a){return dialog_message(a);});
    user("GetWindowThreadProcessId",2,[this](Args a){if(a[1])p.memory.check(a[1],4,Memory::Write);DWORD pid=0;const auto tid=::GetWindowThreadProcessId(message_window(a[0]),&pid);if(a[1])p.memory.store(a[1],pid==::GetCurrentProcessId()?1u:pid,32);return tid==thread?1u:U32(tid);});
    user("AttachThreadInput",3,[this](Args a){return native_bool(::AttachThreadInput(a[0]==1?thread:a[0],a[1]==1?thread:a[1],a[2]!=0));});
    user("WINNLSEnableIME",2,[this](Args a){using Fn=BOOL(WINAPI*)(HWND,BOOL);const auto fn=reinterpret_cast<Fn>(::GetProcAddress(::GetModuleHandleW(L"user32.dll"),"WINNLSEnableIME"));if(!fn){p.set_error(ERROR_CALL_NOT_IMPLEMENTED);return 0u;}return native_bool(fn(hwnd(a[0]),a[1]!=0));});
    user("UnregisterClassA",2,[this](Args a){if(a[1] && a[1]!=p.image_base)unsupported("foreign class instance");auto c=klass(a[0]);auto value=UnregisterClassA(c->native.c_str(),GetModuleHandleW(nullptr));if(value)classes.erase(c->logical);return native_bool(value);});
    user("CreateWindowExA",12,[this](Args a){return create_window(a);});
    user("DestroyWindow",1,[this](Args a){auto h=hwnd(a[0]);for(const auto& [id,handle]:handles)if(handle.kind==Kind::gl){auto d=handles.find(handle.owner);if(d!=handles.end() && d->second.owner==a[0])unsupported("destroy window after releasing its OpenGL contexts");}for(const auto& [key,value]:paints)if(key.first==a[0])unsupported("destroy window with active BeginPaint");auto value=DestroyWindow(h);if(value){handles.erase(a[0]);windows.erase(a[0]);}return native_bool(value);});
    user("IsWindow",1,[this](Args a){auto it=handles.find(a[0]);return U32(it!=handles.end() && (it->second.kind==Kind::window || it->second.kind==Kind::foreign_window) && IsWindow(reinterpret_cast<HWND>(it->second.native)));});
    user("ShowWindow",2,[this](Args a){return U32(ShowWindow(hwnd(a[0]),signed32(a[1])));});
    user("UpdateWindow",1,[this](Args a){return native_bool(UpdateWindow(hwnd(a[0])));});
    user("EnableWindow",2,[this](Args a){return U32(EnableWindow(hwnd(a[0]),a[1]!=0));});
    user("SetWindowPos",7,[this](Args a){return native_bool(SetWindowPos(hwnd(a[0]),zorder(a[1]),signed32(a[2]),signed32(a[3]),signed32(a[4]),signed32(a[5]),a[6]));});
    user("GetClientRect",2,[this](Args a){p.memory.check(a[1],16,Memory::Write);RECT rect{};const auto value=GetClientRect(hwnd(a[0]),&rect);if(value)p.memory.copy_in(a[1],std::span(reinterpret_cast<const std::uint8_t*>(&rect),16));return native_bool(value);});
    user("GetWindowRect",2,[this](Args a){p.memory.check(a[1],16,Memory::Write);RECT rect{};const auto value=GetWindowRect(hwnd(a[0]),&rect);if(value)p.memory.copy_in(a[1],std::span(reinterpret_cast<const std::uint8_t*>(&rect),16));return native_bool(value);});
    user("AdjustWindowRectEx",4,[this](Args a){p.memory.check(a[0],16,Memory::Read|Memory::Write);RECT rect{};auto bytes=read_bytes(p,a[0],16);std::copy(bytes.begin(),bytes.end(),reinterpret_cast<std::uint8_t*>(&rect));const auto value=AdjustWindowRectEx(&rect,a[1],a[2]!=0,a[3]);if(value)p.memory.copy_in(a[0],std::span(reinterpret_cast<const std::uint8_t*>(&rect),16));return native_bool(value);});
    user("DefWindowProcA",4,[this](Args a){return default_window(a);});
    user("CallWindowProcA",5,[this](Args a){if(!a[0])unsupported("null guest window procedure");return p.callback(a[0],a.subspan(1));});
    user("SendMessageA",4,[this](Args a){return send_message(a,false);});
    user("PostMessageA",4,[this](Args a){return send_message(a,true);});
    user("PostQuitMessage",1,[this](Args a){PostQuitMessage(signed32(a[0]));quit_posted=true;return 0u;});
    user("PeekMessageA",5,[this](Args a){return peek_message(a,false);});
    user("GetMessageA",4,[this](Args a){return peek_message(a,true);},0xffffffffu);
    user("TranslateMessage",1,[this](Args a){auto m=read_message(a[0]);return U32(TranslateMessage(&m));});
    user("DispatchMessageA",1,[this](Args a){auto m=read_message(a[0]);++dispatched;return result32(DispatchMessageA(&m));});
    user("GetDesktopWindow",0,[this](Args){return window_token(GetDesktopWindow());});
    user("GetForegroundWindow",0,[this](Args){return window_token(GetForegroundWindow());});
    user("GetActiveWindow",0,[this](Args){return window_token(GetActiveWindow());});
    user("GetFocus",0,[this](Args){return window_token(GetFocus());});
    user("SetFocus",1,[this](Args a){return window_token(SetFocus(hwnd(a[0],true)));});
    user("GetWindowLongA",2,[this](Args a){
        auto h=hwnd(a[0]);const int n=signed32(a[1]);auto it=windows.find(a[0]);if(it==windows.end())return foreign_long(a[0],n,{});auto w=it->second;
        if(w->dialog && (n==0 || n==8))return foreign_long(a[0],n,{});if(w->dialog && n==4)return w->procedure;
        if(n==GWLP_WNDPROC)return w->procedure;if(n==GWLP_USERDATA)return w->user_data;if(n==GWLP_HINSTANCE)return w->create[10];
        if(n==GWLP_HWNDPARENT)return window_token(reinterpret_cast<HWND>(::GetWindowLongPtrA(h,n)));
        if(n==GWL_STYLE || n==GWL_EXSTYLE || n==GWLP_ID)return scalar(std::uintptr_t(::GetWindowLongPtrA(h,n)));
        if(n>=0 && std::uint64_t(n)+4<=w->extra.size()){U32 value=0;for(unsigned i=0;i<4;++i)value|=U32(w->extra[n+i])<<(i*8);return value;}
        unsupported("GetWindowLong index not modelled");
    });
    user("SetWindowLongA",3,[this](Args a){
        auto h=hwnd(a[0]);const int n=signed32(a[1]);auto it=windows.find(a[0]);if(it==windows.end())return foreign_long(a[0],n,a[2]);auto w=it->second;
        if(w->dialog && (n==0 || n==8))return foreign_long(a[0],n,a[2]);if(w->dialog && n==4){p.memory.check(a[2],1,Memory::Execute);const auto previous=w->procedure;w->procedure=a[2];return previous;}
        if(n==GWLP_WNDPROC){p.memory.check(a[2],1,Memory::Execute);const auto prior=w->procedure;w->procedure=a[2];return prior;}
        if(n==GWLP_USERDATA){const auto prior=w->user_data;w->user_data=a[2];return prior;}
        if(n==GWLP_HWNDPARENT){::SetLastError(0);const auto prior=::SetWindowLongPtrA(h,n,reinterpret_cast<LONG_PTR>(message_window(a[2])));if(!prior && ::GetLastError())p.set_error(::GetLastError());return window_token(reinterpret_cast<HWND>(prior));}
        if(n==GWL_STYLE || n==GWL_EXSTYLE || n==GWLP_ID){::SetLastError(0);const auto prior=::SetWindowLongPtrA(h,n,LONG_PTR(signed32(a[2])));if(!prior && ::GetLastError())p.set_error(::GetLastError());return scalar(std::uintptr_t(prior));}
        if(n>=0 && std::uint64_t(n)+4<=w->extra.size()){U32 prior=0;for(unsigned i=0;i<4;++i){prior|=U32(w->extra[n+i])<<(i*8);w->extra[n+i]=std::uint8_t(a[2]>>(i*8));}return prior;}
        unsupported("SetWindowLong index not modelled");
    });
    user("BeginPaint",2,[this](Args a){return paint_begin(a);});
    user("EndPaint",2,[this](Args a){return paint_end(a);});
    user("InvalidateRect",3,[this](Args a){auto h=hwnd(a[0]);RECT r{};RECT* ptr=nullptr;if(a[1]){auto b=read_bytes(p,a[1],16);std::copy(b.begin(),b.end(),reinterpret_cast<std::uint8_t*>(&r));ptr=&r;}return native_bool(InvalidateRect(h,ptr,a[2]!=0));});
    user("FillRect",3,[this](Args a){auto d=dc(a[0]);auto b=read_bytes(p,a[1],16);RECT r{};std::copy(b.begin(),b.end(),reinterpret_cast<std::uint8_t*>(&r));return U32(FillRect(d,&r,brush(a[2])));});
    user("GetDC",1,[this](Args a){auto h=hwnd(a[0],true);auto d=GetDC(h);if(!d){p.set_error(GetLastError());return 0u;}for(auto& [id,entry]:handles)if(entry.kind==Kind::dc && entry.native==reinterpret_cast<std::uintptr_t>(d) && entry.owner==a[0]){++entry.references;return id;}return token(Kind::dc,reinterpret_cast<std::uintptr_t>(d),a[0]);});
    user("ReleaseDC",2,[this](Args a){auto h=hwnd(a[0],true);auto d=get<HDC>(a[1],Kind::dc);auto& entry=handles.at(a[1]);if(entry.owner!=a[0])throw InvalidHandle{6};for(const auto& [id,v]:handles)if(v.kind==Kind::gl && v.owner==a[1])unsupported("ReleaseDC with a live OpenGL context");auto value=ReleaseDC(h,d);if(value && --entry.references==0)handles.erase(a[1]);return U32(value);});
    user("GetCursorPos",1,[this](Args a){p.memory.check(a[0],8,Memory::Write);POINT point{};auto value=GetCursorPos(&point);if(value){p.memory.store(a[0],U32(point.x),32);p.memory.store(a[0]+4,U32(point.y),32);}return native_bool(value);});
    for(bool to_client:{true,false})user(to_client?"ScreenToClient":"ClientToScreen",2,[this,to_client](Args a){p.memory.check(a[1],8,Memory::Read|Memory::Write);POINT pt{signed32(p.memory.load(a[1],32)),signed32(p.memory.load(a[1]+4,32))};auto value=to_client?ScreenToClient(hwnd(a[0]),&pt):ClientToScreen(hwnd(a[0]),&pt);if(value){p.memory.store(a[1],U32(pt.x),32);p.memory.store(a[1]+4,U32(pt.y),32);}return native_bool(value);});
    gdi("GetStockObject",1,[this](Args a){auto obj=GetStockObject(signed32(a[0]));return token(obj && GetObjectType(obj)==OBJ_BRUSH?Kind::brush:Kind::gdi,reinterpret_cast<std::uintptr_t>(obj));});
    gdi("GetDeviceCaps",2,[this](Args a){return U32(::GetDeviceCaps(dc(a[0]),signed32(a[1])));});
    auto pfd=[this](U32 at){PIXELFORMATDESCRIPTOR descriptor{};static_assert(sizeof(descriptor)==40);auto b=read_bytes(p,at,40);std::copy(b.begin(),b.end(),reinterpret_cast<std::uint8_t*>(&descriptor));return descriptor;};
    gdi("ChoosePixelFormat",2,[this,pfd](Args a){auto descriptor=pfd(a[1]);const auto result=ChoosePixelFormat(dc(a[0]),&descriptor);if(!result)p.set_error(GetLastError());return U32(result);});
    gdi("SetPixelFormat",3,[this,pfd](Args a){auto descriptor=pfd(a[2]);return native_bool(SetPixelFormat(dc(a[0]),signed32(a[1]),&descriptor));});
    gdi("GetPixelFormat",1,[this](Args a){const auto result=GetPixelFormat(dc(a[0]));if(!result)p.set_error(GetLastError());return U32(result);});
    gdi("DescribePixelFormat",4,[this](Args a){if(a[3] && a[2]!=40)unsupported("DescribePixelFormat requires the 40-byte guest descriptor");if(a[3])p.memory.check(a[3],40,Memory::Write);PIXELFORMATDESCRIPTOR d{};const auto result=DescribePixelFormat(dc(a[0]),signed32(a[1]),a[3]?40:0,a[3]?&d:nullptr);if(result && a[3])p.memory.copy_in(a[3],std::span(reinterpret_cast<const std::uint8_t*>(&d),40));if(!result)p.set_error(GetLastError());return U32(result);});
    gdi("SwapBuffers",1,[this](Args a){ensure_gl();const auto value=SwapBuffers(dc(a[0]));if(value)++swaps;return native_bool(value);});
    wgl("wglCreateContext",1,[this](Args a){auto h=get<HDC>(a[0],Kind::dc);auto context=wglCreateContext(h);if(!context){p.set_error(GetLastError());return 0u;}++contexts;return token(Kind::gl,reinterpret_cast<std::uintptr_t>(context),a[0]);});
    wgl("wglMakeCurrent",2,[this](Args a){auto context=get<HGLRC>(a[1],Kind::gl,true);auto h=context?get<HDC>(a[0],Kind::dc):nullptr;const auto value=wglMakeCurrent(h,context);if(value){current_gl=a[1];current_dc=a[1]?a[0]:0;}else{current_gl=current_dc=0;p.set_error(GetLastError());}return U32(value);});
    wgl("wglDeleteContext",1,[this](Args a){auto context=get<HGLRC>(a[0],Kind::gl);auto value=wglDeleteContext(context);if(value){if(current_gl==a[0])current_gl=current_dc=0;handles.erase(a[0]);for(auto it=strings.begin();it!=strings.end();)if(it->first.first==a[0]){p.memory.release(it->second);it=strings.erase(it);}else ++it;}return native_bool(value);});
    wgl("wglGetCurrentContext",0,[this](Args){return current_gl;});
    wgl("wglGetCurrentDC",0,[this](Args){return current_dc;});
    wgl("wglGetProcAddress",1,[this](Args a){
        ensure_gl();auto name=p.read_string(a[0],256);
        const auto address=reinterpret_cast<std::uintptr_t>(wglGetProcAddress(name.c_str()));
        if(address<=3 || address==~std::uintptr_t(0)){p.set_error(GetLastError());return 0u;}
        const auto target=p.resolve("opengl32.dll",name);
        if(!target)unsupported("driver exposes an unmodelled WGL export: "+name);
        return target; // Never expose the driver's native function pointer.
    });
    gl("glClearColor",4,[](Args a){glClearColor(real32(a[0]),real32(a[1]),real32(a[2]),real32(a[3]));return 0u;});
    gl("glClear",1,[](Args a){glClear(a[0]);return 0u;});
    gl("glViewport",4,[](Args a){glViewport(signed32(a[0]),signed32(a[1]),signed32(a[2]),signed32(a[3]));return 0u;});
    gl("glBegin",1,[](Args a){glBegin(a[0]);return 0u;});
    gl("glEnd",0,[](Args){glEnd();return 0u;});
    gl("glColor3f",3,[](Args a){glColor3f(real32(a[0]),real32(a[1]),real32(a[2]));return 0u;});
    gl("glColor4f",4,[](Args a){glColor4f(real32(a[0]),real32(a[1]),real32(a[2]),real32(a[3]));return 0u;});
    gl("glVertex2f",2,[](Args a){glVertex2f(real32(a[0]),real32(a[1]));return 0u;});
    gl("glVertex3f",3,[](Args a){glVertex3f(real32(a[0]),real32(a[1]),real32(a[2]));return 0u;});
    gl("glFlush",0,[](Args){glFlush();return 0u;});gl("glFinish",0,[](Args){glFinish();return 0u;});
    gl("glGetError",0,[](Args){return U32(glGetError());});
    gl("glEnable",1,[](Args a){glEnable(a[0]);return 0u;});gl("glDisable",1,[](Args a){glDisable(a[0]);return 0u;});
    gl("glMatrixMode",1,[](Args a){glMatrixMode(a[0]);return 0u;});gl("glLoadIdentity",0,[](Args){glLoadIdentity();return 0u;});
    gl("glPushMatrix",0,[](Args){glPushMatrix();return 0u;});gl("glPopMatrix",0,[](Args){glPopMatrix();return 0u;});
    gl("glTranslatef",3,[](Args a){glTranslatef(real32(a[0]),real32(a[1]),real32(a[2]));return 0u;});
    gl("glRotatef",4,[](Args a){glRotatef(real32(a[0]),real32(a[1]),real32(a[2]),real32(a[3]));return 0u;});
    gl("glScalef",3,[](Args a){glScalef(real32(a[0]),real32(a[1]),real32(a[2]));return 0u;});
    gl("glPixelStorei",2,[](Args a){glPixelStorei(a[0],signed32(a[1]));return 0u;});
    gl("glReadBuffer",1,[](Args a){glReadBuffer(a[0]);return 0u;});gl("glDrawBuffer",1,[](Args a){glDrawBuffer(a[0]);return 0u;});
    gl("glGetString",1,[this](Args a){if(auto it=strings.find({current_gl,a[0]});it!=strings.end())return it->second;auto text=glGetString(a[0]);if(!text)return 0u;std::size_t n=0;while(n<32768 && text[n])++n;if(n==32768)unsupported("driver string exceeds guest limit");auto address=p.put_string(std::string(reinterpret_cast<const char*>(text),n));p.memory.protect(address,(n+1+4095)&~std::size_t(4095),Memory::Read);strings.emplace(std::pair{current_gl,a[0]},address);return address;});
    gl("glGetIntegerv",2,[this](Args a){unsigned count=1;switch(a[0]){case GL_VIEWPORT:case GL_SCISSOR_BOX:count=4;break;case GL_MAX_VIEWPORT_DIMS:count=2;break;case GL_MAX_TEXTURE_SIZE:case GL_MAX_LIGHTS:case GL_MAX_CLIP_PLANES:case GL_PACK_ALIGNMENT:case GL_UNPACK_ALIGNMENT:case GL_RED_BITS:case GL_GREEN_BITS:case GL_BLUE_BITS:case GL_ALPHA_BITS:case GL_DEPTH_BITS:case GL_STENCIL_BITS:case GL_DOUBLEBUFFER:case GL_MATRIX_MODE:case GL_READ_BUFFER:case GL_DRAW_BUFFER:break;default:unsupported("unmodelled glGetIntegerv result shape");}p.memory.check(a[1],count*4,Memory::Read|Memory::Write);std::array<GLint,4> result{};for(unsigned i=0;i<count;++i)result[i]=signed32(p.memory.load(a[1]+4*i,32));glGetIntegerv(a[0],result.data());for(unsigned i=0;i<count;++i)p.memory.store(a[1]+4*i,U32(result[i]),32);return 0u;});
    gl("glReadPixels",7,[this](Args a){
        if(a[4]!=GL_RGBA || a[5]!=GL_UNSIGNED_BYTE)unsupported("bootstrap glReadPixels supports RGBA/UNSIGNED_BYTE only");
        GLint row=0,skip_rows=0,skip_pixels=0,alignment=4;glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);glGetIntegerv(GL_PACK_ROW_LENGTH,&row);glGetIntegerv(GL_PACK_SKIP_ROWS,&skip_rows);glGetIntegerv(GL_PACK_SKIP_PIXELS,&skip_pixels);
        if(row || skip_rows || skip_pixels)unsupported("non-default pixel pack layout is not implemented");
        if(signed32(a[2])<0 || signed32(a[3])<0){glReadPixels(signed32(a[0]),signed32(a[1]),signed32(a[2]),signed32(a[3]),a[4],a[5],nullptr);return 0u;}
        if(alignment!=1 && alignment!=2 && alignment!=4 && alignment!=8)unsupported("invalid driver pixel pack alignment");
        const auto row_bytes=std::uint64_t(a[2])*4;
        const auto stride=(row_bytes+unsigned(alignment)-1)&~std::uint64_t(unsigned(alignment)-1);
        const auto bytes=a[2] && a[3]?stride*(a[3]-1)+row_bytes:0;
        if(bytes>16u*1024*1024)unsupported("pixel readback exceeds bounded staging buffer");
        p.memory.check(a[6],std::size_t(bytes),Memory::Read|Memory::Write);auto buffer=read_bytes(p,a[6],std::size_t(bytes));
        glReadPixels(signed32(a[0]),signed32(a[1]),signed32(a[2]),signed32(a[3]),a[4],a[5],buffer.data());p.memory.copy_in(a[6],buffer);return 0u;
    });
    user("RegisterWindowMessageA",1,[this](Args a){
        const auto name=p.read_string(a[0],256);const auto id=RegisterWindowMessageA(name.c_str());
        if(id)guest_registered_messages.insert(id);else p.set_error(GetLastError());return U32(id);
    });
    user("RegisterWindowMessageW",1,[this](Args a){
        const auto name=p.read_wstring(a[0],256);const auto id=RegisterWindowMessageW(reinterpret_cast<const wchar_t*>(name.c_str()));
        if(id)guest_registered_messages.insert(id);else p.set_error(GetLastError());return U32(id);
    });
}
