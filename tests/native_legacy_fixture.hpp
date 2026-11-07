#pragma once
#include "native_scene_fixture.hpp"

constexpr unsigned scene_at=0x110a0,script_at=0x134d4,record_bytes=0xd190;
void half(Bytes& b,std::size_t at,unsigned value){b.at(at)=std::uint8_t(value);b.at(at+1)=std::uint8_t(value>>8);}
void string(Bytes& b,std::size_t at,std::string_view value){std::copy(value.begin(),value.end(),b.begin()+std::ptrdiff_t(at));b.at(at+value.size())=0;}
void field(Bytes& b,unsigned at,int value){put(b,scene_at+at,std::bit_cast<unsigned>(value));}
void real(Bytes& b,unsigned at,float value){put(b,scene_at+at,std::bit_cast<unsigned>(value));}
void label(Bytes& b,unsigned at,std::string_view value){string(b,scene_at+at,value);}
void small(Bytes& b,unsigned at,int value){half(b,scene_at+at,unsigned(value));}
Bytes frame(const s::Program& program,std::optional<unsigned> pending={}){
    Bytes b(record_bytes);put(b,4,program.version());put(b,8,unsigned(program.labels().size()));for(unsigned n=0;n<program.labels().size();++n){put(b,12+n*4,std::bit_cast<unsigned>(program.labels()[n].id));put(b,0x40c+n*4,program.labels()[n].offset);}
    put(b,0x80c,2);put(b,0x8b40,std::bit_cast<unsigned>(program.labels().front().id));put(b,0x8cac,20);string(b,0xcef8,program.name());string(b,0xcf3c,"ck-gal");
    if(pending){const auto site=std::find_if(program.commands().begin(),program.commands().end(),[&](auto command){return command.index==*pending;});CHECK(site!=program.commands().end());put(b,0x8cac,site->offset+8);put(b,0x8b34,1);put(b,0x8b38,4);put(b,0x8b3c,*pending);put(b,0x8b44,1);
        const auto types=signatures.at(*pending);for(unsigned n=0;n<types.size();++n){const auto at=0x8b4c+n*20;put(b,at+4,types[n]==5?5:3);put(b,at+8,types[n]==5?0xf0aabbcc:0);put(b,at+16,n==1?7:n==2?1:0);}}
    return b;
}
Bytes file(std::initializer_list<Bytes> frames){
    Bytes b(script_at+12+5*record_bytes);put(b,0,2);put(b,4,1001001);half(b,8,2024);half(b,10,2);half(b,12,4);half(b,14,29);string(b,24,"OWNED SAVE");string(b,script_at,"1.00");put(b,script_at+4,4);put(b,script_at+8,12+5*record_bytes);
    unsigned n{};for(const auto& record:frames){CHECK(record.size()==record_bytes);std::copy(record.begin(),record.end(),b.begin()+script_at+12+std::ptrdiff_t(n++)*record_bytes);}
    const auto globals=b.size();word(b,0);word(b,1);word(b,7);b.insert(b.end(),{'F','L','G','_','0','0',0,3});word(b,0);put(b,globals,unsigned(b.size()-globals));
    field(b,0x54,256);field(b,0x58,256);field(b,0x110c,-1);field(b,0xef4,42);for(unsigned n=0;n<4;++n)field(b,0xefc+n*4,-1);for(unsigned at:{0x1100,0x1104,0x1108})field(b,at,128);
    b[scene_at+0x210]=1;b[scene_at+0x218]=1;small(b,0x220,7);small(b,0x21c,2);label(b,0x222,"ABC\\kDEF");small(b,0x622,1);label(b,0x624,"OWNED");b[scene_at+0x212]=10;return b;
}
