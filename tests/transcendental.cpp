#include "fp_registers.hpp"
#include <iostream>
#include <random>
#include <map>
#include <fstream>
namespace f=wa2::aot::fp;
template<unsigned Op>void hardware(wr::FpState& state){
    alignas(16) std::array<std::uint8_t,512> saved{},image{},result{};
    std::copy(state.bytes.begin(),state.bytes.end(),image.begin());
    asm volatile("fxsave64 %0\n\tfxrstor64 %1\n\t.byte 0xd9,%c3\n\tfxsave64 %2\n\tfxrstor64 %0"
        :"=m"(saved),"+m"(image),"=m"(result):"i"(Op):"memory");
    std::copy_n(result.begin(),state.bytes.size(),state.bytes.begin());
}
template<unsigned Op>void registers(wr::FpState& state){
    f::detail::RegisterStack stack;stack.load(state);stack.begin();
    f::x87_operation<0xd9,Op,false>(stack,nullptr);stack.finish();stack.save(state);
}
struct Form {unsigned encoding;void(*portable)(void*,void*,std::uint32_t*);void(*native)(wr::FpState&);void(*registers)(wr::FpState&);};
#define FORM(op) Form{op,&f::x87<0xd9,op,false>,&hardware<op>,&registers<op>}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    const Form forms[]={FORM(0xf0),FORM(0xf1),FORM(0xf9),FORM(0xf3),FORM(0xfe),FORM(0xff),FORM(0xf2)};
    constexpr std::uint64_t values[]={0,0x8000000000000000ull,0x3ff0000000000000ull,0xbff0000000000000ull,
        0x3fd5555555555555ull,0x400921fb54442d18ull,0x3f847ae147ae147bull,0xc004000000000000ull,
        0x0010000000000000ull,0x7fefffffffffffffull,0x7ff0000000000000ull,0x7ff8000000001234ull};
    std::mt19937_64 random(12345);unsigned cases{},mismatches{};std::map<unsigned,unsigned> differences;
    for(const auto form:forms)for(unsigned rounding=0;rounding<4;++rounding)for(const auto precision:{0u,2u,3u})for(unsigned variant=0;variant<1280;++variant){
        const auto top=variant%8;
        wr::FpState p;p.put16(0,0x7f|(precision<<8)|(rounding<<10));p.put16(2,(top<<11)|((variant&8)?0x4700:0)|((variant&16)?0x3f:0));p.bytes[4]=std::uint8_t(0xff^(1u<<((top+7)&7)));p.put32(28,0xffff);
        for(unsigned n=0;n<8;++n){f::Environment env;auto x=f::from_binary(values[(variant+n)%std::size(values)],64,env);
            if(variant>=24 && variant<512){
                constexpr unsigned edges[]={0,1,2,0x3bff,0x3ffe,0x3fff,0x4000,0x403e,0x403f,0x7ffe,0x7fff};
                const auto exponent=variant&1?edges[random()%std::size(edges)]:unsigned(random()%32768);
                x={random()|0x8000000000000000ull,std::uint16_t(exponent|((random()&1)<<15))};
            }
            if(variant>=512 && variant<768)x={random()&0x7fffffffffffffffull,std::uint16_t((random()&1)<<15)};
            if(variant>=768 && variant<1024)x={random()&0x7fffffffffffffffull,std::uint16_t((1+random()%32767)|((random()&1)<<15))};
            p.put64(32+n*16,x.significand);p.put16(40+n*16,x.sign_exponent);
        }
        if(variant>=896 && variant<1024)p.bytes[4]=std::uint8_t(random());
        if(variant>=1024)p.bytes[4]=std::uint8_t(variant-1024);
        auto h=p,r=p;std::uint32_t flags{};form.native(h);form.portable(p.bytes.data(),nullptr,&flags);form.registers(r);++cases;
        if(r.bytes!=p.bytes){std::cerr<<"serialized/register-file mismatch\n";return 1;}
        bool same=p.get16(0)==h.get16(0) && p.get16(2)==h.get16(2) && p.bytes[4]==h.bytes[4];
        for(unsigned n=0;n<8;++n)if(h.bytes[4]&(1u<<((h.top()+n)&7)))same&=p.get64(32+n*16)==h.get64(32+n*16) && p.get16(40+n*16)==h.get16(40+n*16);
        if(!same){if(differences[form.encoding]++<4)std::cout<<std::hex<<"op "<<form.encoding<<" rc "<<rounding<<" pc "<<precision<<" variant "<<variant
            <<" sw "<<p.get16(2)<<'/'<<h.get16(2)<<" st0 "<<p.get16(40)<<':'<<p.get64(32)<<'/'<<h.get16(40)<<':'<<h.get64(32)<<std::dec<<'\n';++mismatches;}
    }
    std::cout<<"cases "<<cases<<" mismatches "<<mismatches<<'\n';for(auto [op,count]:differences)std::cout<<std::hex<<op<<std::dec<<' '<<count<<'\n';
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.x87-transcendental-check.v1\",\"cases\":"<<cases<<",\"mismatches\":"<<mismatches<<",\"strict_status_and_live_registers\":true,\"register_file_agreement\":true,\"hardware_oracle\":true,\"source_executable_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":"<<(mismatches?"false":"true")<<"}\n";
    return mismatches || !report?1:0;
}
