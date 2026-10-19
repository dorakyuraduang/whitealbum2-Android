U32 Gui::foreign_long(U32 id,int index,std::optional<U32> value){
    auto native=hwnd(id);std::array<char,64> name{};::GetClassNameA(native,name.data(),int(name.size()));const bool dialog=std::string_view(name.data())=="#32770";
    if(index==GWLP_USERDATA || (dialog && index==8)){auto& data=foreign_data[{id,index}];const auto previous=data;if(value)data=*value;return previous;}
    if(index==GWLP_HWNDPARENT){if(value)return window_token(reinterpret_cast<HWND>(::SetWindowLongPtrA(native,index,reinterpret_cast<LONG_PTR>(message_window(*value)))));return window_token(reinterpret_cast<HWND>(::GetWindowLongPtrA(native,index)));}
    if(index==GWL_STYLE || index==GWL_EXSTYLE || index==GWLP_ID || (dialog && index==0)){
        const auto slot=dialog && index==0?DWLP_MSGRESULT:index;::SetLastError(0);
        const auto result=value?::SetWindowLongPtrA(native,slot,signed_param(*value)): ::GetWindowLongPtrA(native,slot);if(!result && ::GetLastError())p.set_error(::GetLastError());return scalar(std::uintptr_t(result));
    }
    unsupported("native control window-long slot needs an explicit guest layout");
}
U32 Gui::dialog_box(Args a,bool modal){
    if(a[0] && a[0]!=p.image_base){p.set_error(ERROR_INVALID_HANDLE);return modal?0xffffffffu:0;}
    p.memory.check(a[3],1,Memory::Execute);auto resource=find_resource(p.source_image(),std::uint16_t(5),gui_resource_name(p,a[1]));
    if(!resource){p.set_error(ERROR_RESOURCE_NAME_NOT_FOUND);return modal?0xffffffffu:0;}
    const auto bytes=resource_bytes(p.source_image(),*resource);if(bytes.size()<18)unsupported("truncated dialog resource");
    auto w=std::make_shared<Window>();w->gui=this;w->klass=std::make_shared<WindowClass>();w->klass->logical="#32770";w->procedure=a[3];w->dialog=true;w->modal=modal;w->dialog_parameter=a[4];w->create[10]=p.image_base;
    w->id=token(Kind::window,1,0,false,false);windows.emplace(w->id,w);creating_dialogs.push_back(w);
    const auto* data=reinterpret_cast<const DLGTEMPLATE*>(bytes.data());const auto owner=message_window(a[2]);
    const auto result=modal?::DialogBoxIndirectParamA(::GetModuleHandleW(nullptr),data,owner,dialog_procedure,reinterpret_cast<LPARAM>(w.get())):
        reinterpret_cast<INT_PTR>(::CreateDialogIndirectParamA(::GetModuleHandleW(nullptr),data,owner,dialog_procedure,reinterpret_cast<LPARAM>(w.get())));
    const auto error=::GetLastError();creating_dialogs.pop_back();
    if(modal || !result){if(w->alive)::DestroyWindow(w->native);window_owners.erase(w->native);windows.erase(w->id);handles.erase(w->id);}
    if(!result || result==-1)p.set_error(error);check();return modal?U32(result):result?w->id:0;
}
INT_PTR CALLBACK Gui::dialog_procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) noexcept {
    std::shared_ptr<Window> w;
    try{
        if(auto it=window_owners.find(hwnd);it!=window_owners.end())w=it->second.lock();
        if(!w && !creating_dialogs.empty()){
            w=creating_dialogs.back();w->native=hwnd;w->alive=true;w->counted=true;++w->gui->created;
            window_owners[hwnd]=w;w->gui->handles.at(w->id).native=reinterpret_cast<std::uintptr_t>(hwnd);
        }
        if(!w)return FALSE;auto& g=*w->gui;
        auto forget=[&]{if(message==WM_NCDESTROY){window_owners.erase(hwnd);w->alive=false;++g.destroyed;}};
        if(g.stopping || g.failure || g.p.exited()){forget();return FALSE;}
        if(g.host_message(message)){++g.host_only;g.host_only_ids.insert(message);return FALSE;}
        U32 result=0;
        if(message==WM_INITDIALOG){const std::array<U32,4> args{w->id,message,g.window_token(reinterpret_cast<HWND>(wp)),w->dialog_parameter};result=g.p.callback(w->procedure,args);}
        else{Frame frame(g,w,message,wp,lp);const std::array<U32,4> args{w->id,message,frame.wp,frame.lp};result=g.p.callback(w->procedure,args);frame.to_native();
            if(message==WM_GETTEXT && frame.wp){const auto count=std::min<std::size_t>(std::uint64_t(result)+1,frame.wp);auto text=read_bytes(g.p,frame.lp,count);std::copy(text.begin(),text.end(),reinterpret_cast<std::uint8_t*>(lp));}}
        ++g.callbacks;forget();
        if(message>=WM_CTLCOLORMSGBOX && message<=WM_CTLCOLORSTATIC)return reinterpret_cast<INT_PTR>(g.brush(result));
        return signed_param(result);
    }catch(...){if(w){auto& g=*w->gui;if(!g.failure)g.failure=std::current_exception();if(w->modal)::EndDialog(hwnd,-1);else ::PostMessageA(hwnd,WM_CLOSE,0,0);}return FALSE;}
}
U32 Gui::dialog_message(Args a){
    auto target=::GetDlgItem(hwnd(a[0]),signed32(a[1]));if(!target){p.set_error(ERROR_CONTROL_ID_NOT_FOUND);return 0;}
    return control_message(target,a[2],a[3],a[4]);
}
U32 Gui::control_message(HWND target,U32 message,U32 first,U32 second){
    const std::array<U32,5> a{0,0,message,first,second};WPARAM wp=first;LPARAM lp=signed_param(second);
    std::string text;std::vector<std::uint8_t> bytes;bool output=false;
    if(message==WM_USER+102){if(first){text=guest_file_path(p,p.read_string(second)).string();lp=reinterpret_cast<LPARAM>(text.c_str());}else{if(!p.state().native_pidl)unsupported("missing native PIDL bridge");lp=LPARAM(p.state().native_pidl(second));}}
    else if(message==WM_USER+100){text=p.read_string(second);lp=reinterpret_cast<LPARAM>(text.c_str());}
    else if(message==WM_SETTEXT || message==CB_ADDSTRING || message==CB_INSERTSTRING || message==LB_ADDSTRING || message==LB_INSERTSTRING || message==CB_FINDSTRING || message==CB_FINDSTRINGEXACT || message==LB_FINDSTRING || message==LB_FINDSTRINGEXACT){text=p.read_string(a[4]);lp=reinterpret_cast<LPARAM>(text.c_str());}
    else if(message==WM_GETTEXT){if(a[3]>1048576)unsupported("oversized dialog control text");p.memory.check(a[4],a[3],Memory::Write);bytes.resize(a[3]);lp=reinterpret_cast<LPARAM>(bytes.data());output=true;}
    else if(message==CB_GETLBTEXT || message==LB_GETTEXT){const auto length=::SendMessageA(target,message==CB_GETLBTEXT?CB_GETLBTEXTLEN:LB_GETTEXTLEN,wp,0);if(length<0)return U32(length);if(length>1048576)unsupported("oversized list text");p.memory.check(a[4],std::size_t(length)+1,Memory::Write);bytes.resize(std::size_t(length)+1);lp=reinterpret_cast<LPARAM>(bytes.data());output=true;}
    else if(message==EM_GETSEL){DWORD start=0,end=0;if(first)p.memory.check(first,4,Memory::Write);if(second)p.memory.check(second,4,Memory::Write);const auto result=::SendMessageA(target,message,first?reinterpret_cast<WPARAM>(&start):0,second?reinterpret_cast<LPARAM>(&end):0);if(first)p.memory.store(first,start,32);if(second)p.memory.store(second,end,32);return scalar(std::uintptr_t(result));}
    else if(message==CB_GETDROPPEDCONTROLRECT || message==TBM_GETTHUMBRECT || message==TBM_GETCHANNELRECT){p.memory.check(second,16,Memory::Write);bytes.resize(16);lp=reinterpret_cast<LPARAM>(bytes.data());output=true;}
    else if(message==EM_SETSEL || message==BM_GETCHECK || message==BM_SETCHECK || message==BM_GETSTATE || message==BM_SETSTATE || message==BM_SETSTYLE || message==BM_CLICK || message==CB_SETCURSEL || message==CB_GETCURSEL || message==CB_GETCOUNT || message==CB_RESETCONTENT || message==LB_SETCURSEL || message==LB_GETCURSEL || message==LB_GETCOUNT || message==LB_RESETCONTENT || message==TBM_SETRANGE || message==TBM_SETRANGEMIN || message==TBM_SETRANGEMAX || message==TBM_SETPOS || message==TBM_GETPOS || message==TBM_SETTICFREQ){}
    else if(message==WM_DEVICECHANGE && second){
        p.memory.check(second,12,Memory::Read);const auto size=p.memory.load(second,32),type=p.memory.load(second+4,32);
        try{check_device_broadcast_size(size,type);bytes=read_bytes(p,second,size);check_device_broadcast(bytes);}
        catch(const GuestFault&){throw;}catch(const std::runtime_error& e){unsupported(e.what());}
        lp=reinterpret_cast<LPARAM>(bytes.data());
    }
    else if(!scalar_payload(message,second))unsupported("dialog control message needs an explicit native layout: "+hex(message));
    if(output && !bytes.empty())p.memory.copy_out(a[4],bytes);
    const auto result=::SendMessageA(target,message,wp,lp);check();if(output && result>=0 && !bytes.empty())p.memory.copy_in(a[4],bytes);return scalar(std::uintptr_t(result));
}
