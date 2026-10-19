// Modern host transport/non-client messages keep their native payloads and
// native handling for queued delivery as well as synchronous WNDPROC calls.
bool Gui::host_message(UINT message) const {
    if(message==0x60 || message==0x118 || (message>=0x90 && message<=0x95) || message==0xae || message==0xaf || message==WM_GETOBJECT || message==WM_IME_REQUEST || (message>=0x31e && message<=0x321) || message==0x33f || message==0x02e0)return true;
    // Host components register their own protocols, including pointer-bearing
    // IME queries. IDs vary by desktop; only registrations made through the
    // guest API belong to the guest. Others follow the native default path.
    return message>=0xc000 && !guest_registered_messages.contains(message);
}
// System message payloads which contain neither pointers nor native handles.
// No 'all small integers are safe' heuristic is used for unknown system IDs.
bool scalar_message(UINT m){
    if(m==WM_CHANGEUISTATE || m==WM_UPDATEUISTATE || m==WM_QUERYUISTATE || m==WM_GETFONT)return true;
    if(m>=WM_USER)return true;
    if((m>=WM_KEYFIRST && m<=WM_KEYLAST) || (m>=WM_MOUSEFIRST && m<=WM_MOUSELAST) || (m>=WM_NCMOUSEMOVE && m<=WM_NCXBUTTONDBLCLK))return true;
    switch(m){case WM_NULL:case WM_DESTROY:case WM_MOVE:case WM_SIZE:case WM_ENABLE:case WM_SETREDRAW:case WM_PAINT:case WM_CLOSE:case WM_QUERYENDSESSION:case WM_QUERYOPEN:case WM_ENDSESSION:case WM_SHOWWINDOW:case WM_CANCELMODE:case WM_CHILDACTIVATE:case WM_GETTEXTLENGTH:case WM_TIMECHANGE:case WM_SYSCOLORCHANGE:case WM_NCDESTROY:case WM_NCHITTEST:case WM_SYNCPAINT:case WM_SYSCOMMAND:case WM_DISPLAYCHANGE:case WM_ENTERMENULOOP:case WM_EXITMENULOOP:case WM_ENTERSIZEMOVE:case WM_EXITSIZEMOVE:case WM_MOUSEHOVER:case WM_MOUSELEAVE:case WM_NCMOUSEHOVER:case WM_NCMOUSELEAVE:case WM_IME_SETCONTEXT:case WM_IME_NOTIFY:case WM_IME_STARTCOMPOSITION:case WM_IME_ENDCOMPOSITION:case WM_IME_COMPOSITION:case WM_IME_CHAR:case WM_IME_KEYDOWN:case WM_IME_KEYUP:case WM_TIMER:case WM_QUIT:case WM_DWMCOMPOSITIONCHANGED:case WM_DWMNCRENDERINGCHANGED:case WM_DWMCOLORIZATIONCOLORCHANGED:case WM_DWMWINDOWMAXIMIZEDCHANGE:return true;default:return false;}
}
bool scalar_payload(UINT message,std::uintptr_t payload){
    return scalar_message(message) || (message==WM_DEVICECHANGE && !payload);
}
// These DEV_BROADCAST forms contain fixed-width scalars, a GUID and/or an
// inline ANSI name; their x86 and x64 layouts agree. HANDLE notifications need
// distinct handle/registration ownership and are deliberately not copied raw.
void check_device_broadcast_size(U32 size,U32 type){
    U32 minimum=0;
    switch(type){case DBT_DEVTYP_OEM:minimum=20;break;case 1:minimum=16;break;
    case DBT_DEVTYP_VOLUME:minimum=20;break;case DBT_DEVTYP_PORT:minimum=13;break;
    case DBT_DEVTYP_DEVICEINTERFACE:minimum=29;break;default:break;}
    if(!minimum || size<minimum || size>32768 || ((type==DBT_DEVTYP_OEM || type==1 || type==DBT_DEVTYP_VOLUME) && size!=minimum))
        throw std::runtime_error("unsupported device broadcast layout/length");
}
void check_device_broadcast(std::span<const std::uint8_t> bytes){
    U32 size=0,type=0;std::memcpy(&size,bytes.data(),4);std::memcpy(&type,bytes.data()+4,4);
    check_device_broadcast_size(size,type);
    if(bytes.size()!=size)throw std::runtime_error("device broadcast length changed");
    const auto name=type==DBT_DEVTYP_PORT?12u:type==DBT_DEVTYP_DEVICEINTERFACE?28u:0u;
    if(name && !std::memchr(bytes.data()+name,0,size-name))throw std::runtime_error("unterminated device broadcast name");
}
