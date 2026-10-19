#include "fp_kernel_cases.hpp"
#include "x87_kernel.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <sstream>

namespace f=wa2::aot::fp;
std::string state(const wr::FpState& value){
    std::ostringstream out;out<<std::hex<<"cw="<<value.get16(0)<<" sw="<<value.get16(2)<<" tags="<<unsigned(value.bytes[4]);
    for(unsigned i=0;i<3;++i)out<<" st"<<i<<'='<<value.get16(40+i*16)<<':'<<value.get64(32+i*16);
    return out.str();
}
bool transcendent(const FpKernelCase& c){return !c.memory && c.opcode==0xd9 && (c.encoding==0xf0 || c.encoding==0xf1 || c.encoding==0xf2 || c.encoding==0xf3 || c.encoding==0xf9 || c.encoding==0xfe || c.encoding==0xff);}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("report path required");
    unsigned total{},failures{},strict{},narrowed{},extended_cases{},stack_cases{},register_cases{};
    std::mt19937_64 random(0x574132);
    constexpr std::uint64_t inputs[]={0,0x8000000000000000ull,0x3ff0000000000000ull,0xbff0000000000000ull,
        0x3fd5555555555555ull,0x400921fb54442d18ull,0x3f847ae147ae147bull,0xc004000000000000ull,
        0x0010000000000000ull,0x7fefffffffffffffull,0x7ff0000000000000ull,0x7ff8000000001234ull};
    std::ostringstream differences;
    std::map<std::string,unsigned> by_form;
    for(const auto& c:fp_kernel_cases())for(unsigned rounding=0;rounding<4;++rounding)for(unsigned pc:{0u,2u,3u})for(unsigned variant=0;variant<96;++variant){
        // Transcendentals retain an explicit, narrower contract and are not
        // admitted into the shared game yet. Stress every strict 80-bit form.
        if(transcendent(c) && variant>=24)continue;
        wr::FpState portable;
        portable.put16(0,std::uint16_t(0x7f|(pc<<8)|(rounding<<10)));
        const unsigned top=variant>=64?(variant-64)%8:0;
        portable.put16(2,std::uint16_t(top<<11));portable.bytes[4]=std::uint8_t(0xffu^(1u<<((top+7)&7)));portable.put32(28,0xffff);
        for(unsigned i=0;i<8;++i){
            f::Environment e;auto value=f::from_binary(inputs[(variant+i)%std::size(inputs)],64,e);
            if(variant>=24){
                auto significand=random()|0x8000000000000000ull;
                unsigned exponent=0x3fff+int(random()%257)-128;
                if(variant>=48){
                    constexpr unsigned edges[]={0,1,2,0x7ffe,0x7fff,0x3fff,0x4000,0x3ffe};
                    exponent=edges[(variant+i)%8];
                    if(!exponent)significand&=0x7fffffffffffffffull;
                    if(exponent==0x7fff)significand|=0x4000000000000000ull;
                }
                value={significand,std::uint16_t(exponent|((random()&1)<<15))};
            }
            portable.put64(32+i*16,value.significand);portable.put16(40+i*16,value.sign_exponent);
        }
        // Arithmetic uses ST0 and STi; loads have a free physical push slot.
        if(c.opcode==0xd8 || c.opcode==0xdc || c.opcode==0xde)portable.bytes[4]=0xff;
        if(variant>=64){portable.bytes[4]=std::uint8_t(random());++stack_cases;}
        if(variant>=24)++extended_cases;
        alignas(16) std::array<std::uint8_t,16> left{};
        auto memory_bits=inputs[variant%std::size(inputs)];
        for(unsigned i=0;i<8;++i)left[i]=std::uint8_t(memory_bits>>(i*8));
        if(c.memory && c.opcode==0xd9 && c.encoding==5){left[0]=std::uint8_t(portable.get16(0));left[1]=std::uint8_t(portable.get16(0)>>8);}
        const bool extended=c.memory && c.opcode==0xdb && (c.encoding==5 || c.encoding==7);
        if(extended){f::Environment e;const auto value=f::from_binary(memory_bits,64,e);for(unsigned i=0;i<8;++i)left[i]=std::uint8_t(value.significand>>(i*8));left[8]=std::uint8_t(value.sign_exponent);left[9]=std::uint8_t(value.sign_exponent>>8);}
        auto native=portable,registers=portable;auto right=left,register_operand=left;std::uint32_t flags=0x202,native_flags=flags,register_flags=flags;
        c.native(native.bytes.data(),right.data(),&native_flags);c.portable(portable.bytes.data(),left.data(),&flags);
        c.registers(registers.bytes.data(),register_operand.data(),&register_flags);
        if(registers.bytes!=portable.bytes || register_operand!=left || register_flags!=flags){
            throw std::runtime_error(std::string("register-file mismatch: ")+c.name+" variant "+std::to_string(variant)+" pc "+std::to_string(pc)+" rc "+std::to_string(rounding));
        }
        ++register_cases;
        bool same=portable.get16(0)==native.get16(0) && portable.bytes[4]==native.bytes[4] && left==right;
        const bool approximate=transcendent(c);
        const unsigned status_mask=approximate?0xfdff:0xffff;
        same&=(portable.get16(2)&status_mask)==(native.get16(2)&status_mask);
        for(unsigned i=0;i<8;++i){
            if(!(native.bytes[4]&(1u<<((native.top()+i)&7))))continue;
            const f::Extended p{portable.get64(32+i*16),portable.get16(40+i*16)},n{native.get64(32+i*16),native.get16(40+i*16)};
            if(approximate && !p.nan() && !n.nan()){
                f::Environment ep,en;const auto pd=f::to_binary(p,32,ep),nd=f::to_binary(n,32,en);same&=pd==nd;
            }else same&=p==n;
        }
        ++total;if(approximate)++narrowed;else ++strict;
        if(!same){
            const auto first_mismatch=by_form[c.name]++==0;
            if(first_mismatch || (rounding==0 && pc==3))differences<<c.name<<" rc="<<rounding<<" pc="<<pc<<" variant="<<variant
                <<"\n portable "<<state(portable)<<"\n native   "<<state(native)
                <<"\n operand "<<std::hex<<f::detail::read_word(left.data(),8)<<'/'<<f::detail::read_word(right.data(),8)<<std::dec<<'\n';
            ++failures;
        }
    }
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.portable-x87-kernel.v1\",\"forms\":"<<fp_kernel_cases().size()<<",\"cases\":"<<total
        <<",\"strict_cases\":"<<strict<<",\"transcendental_float32_cases\":"<<narrowed<<",\"failures\":"<<failures
        <<",\"random_extended_precision_cases\":"<<extended_cases<<",\"random_stack_occupancy_cases\":"<<stack_cases
        <<",\"exact_register_file_cases\":"<<register_cases
        <<",\"source_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"passed\":"<<(failures?"false":"true")<<"}\n";
    if(failures){std::cerr<<differences.str();for(const auto& [name,count]:by_form)std::cerr<<name<<' '<<count<<'\n';std::cerr<<failures<<" / "<<total<<" mismatches\n";return 1;}
    std::cout<<"PASS portable x87 "<<total<<" cases\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
