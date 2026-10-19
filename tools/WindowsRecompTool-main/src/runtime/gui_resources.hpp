ResourceName gui_resource_name(Process& p,U32 at){
    if(at<65536)return std::uint16_t(at);
    const auto text=p.read_string(at,4096);std::u16string wide;
    for(unsigned char ch:text){if(ch>=128)throw GuestFault(FaultKind::unsupported,at,"non-ASCII GUI resource name");wide+=char16_t(ch);}return wide;
}
U32 Gui::load_menu(Args a){
    if(a[0] && a[0]!=p.image_base)unsupported("foreign menu resource module");
    auto item=find_resource(p.source_image(),std::uint16_t(4),gui_resource_name(p,a[1]));
    if(!item){p.set_error(ERROR_RESOURCE_NAME_NOT_FOUND);return 0;}
    const auto data=resource_bytes(p.source_image(),*item);
    auto word=[&](std::size_t at){if(at+2>data.size())unsupported("truncated menu resource");return unsigned(data[at])|(unsigned(data[at+1])<<8);};
    const auto version=word(0);if(version>1)unsupported("unknown menu resource version");
    std::size_t offset=4+word(2);unsigned items=0;
    auto walk=[&](auto&& self,unsigned depth)->void {
        if(depth>32)unsupported("menu resource nesting limit");
        for(;;){if(++items>4096)unsupported("menu resource item limit");unsigned flags=0;
            if(version){offset=(offset+3)&~std::size_t(3);for(unsigned n=0;n<6;++n)word(offset+n*2);flags=word(offset+12);offset+=14;}
            else{flags=word(offset);offset+=2;if(!(flags&MF_POPUP)){word(offset);offset+=2;}}
            for(;;){const auto ch=word(offset);offset+=2;if(!ch)break;}
            if(flags&(version?1u:unsigned(MF_POPUP))){if(version){offset=(offset+3)&~std::size_t(3);word(offset);word(offset+2);offset+=4;}self(self,depth+1);}if(flags&MF_END)break;
        }
    };walk(walk,0);
    std::vector<std::uint8_t> copied(data.begin(),data.end());auto result=::LoadMenuIndirectW(reinterpret_cast<const MENUTEMPLATEW*>(copied.data()));
    if(!result){p.set_error(::GetLastError());return 0;}return token(Kind::menu,reinterpret_cast<std::uintptr_t>(result),0,true);
}
U32 Gui::load_accelerators(Args a){
    if(a[0] && a[0]!=p.image_base)unsupported("foreign accelerator module");
    auto item=find_resource(p.source_image(),std::uint16_t(9),gui_resource_name(p,a[1]));
    if(!item){p.set_error(ERROR_RESOURCE_NAME_NOT_FOUND);return 0;}
    const auto data=resource_bytes(p.source_image(),*item);if(data.empty() || data.size()%8 || data.size()>4096*8)unsupported("invalid accelerator resource size");
    auto word=[&](std::size_t at){return WORD(unsigned(data[at])|(unsigned(data[at+1])<<8));};
    std::vector<ACCEL> rows;
    for(std::size_t off=0;off<data.size();off+=8){const auto flags=word(off);rows.push_back(ACCEL{BYTE(flags&0x7f),word(off+2),word(off+4)});if(flags&0x80)break;}
    const auto result=::CreateAcceleratorTableA(rows.data(),int(rows.size()));if(!result){p.set_error(::GetLastError());return 0;}
    return token(Kind::accelerator,reinterpret_cast<std::uintptr_t>(result),0,true);
}
