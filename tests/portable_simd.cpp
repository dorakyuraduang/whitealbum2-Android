#include "fp_kernel_cases.hpp"
#include "sse_kernel.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <sstream>

namespace f=wa2::aot::fp;
std::string state(const wr::FpState& s,unsigned reg){
    std::ostringstream out;out<<std::hex<<"mxcsr="<<s.get32(24)<<" xmm"<<reg<<'='<<s.get64(160+reg*16)<<':'<<s.get64(168+reg*16);return out.str();
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("report path required");
    std::mt19937_64 random(0x53494d44);unsigned total{},failures{};std::map<std::string,unsigned> by_form;std::ostringstream differences;
    constexpr std::uint64_t values[]={0,0x8000000000000000ull,0x3ff0000000000000ull,0xbff0000000000000ull,
        0x3fd5555555555555ull,0x400921fb54442d18ull,0x3f847ae147ae147bull,0xc004000000000000ull,
        0x0010000000000000ull,0x7fefffffffffffffull,0x7ff0000000000000ull,0x7ff8000000001234ull,
        1,0x000fffffffffffffull,0xfff0000000000000ull,0x7ff0000000000001ull};
    for(const auto& c:simd_kernel_cases())for(unsigned rounding=0;rounding<4;++rounding)for(unsigned mode=0;mode<4;++mode)for(unsigned variant=0;variant<24;++variant){
        wr::FpState portable;portable.put32(24,0x1f80|(rounding<<13)|(mode&1?0x40:0)|(mode&2?0x8000:0));portable.put32(28,0xffff);
        for(unsigned i=0;i<16;++i)portable.put64(160+i*8,variant<16?values[(variant+i)%std::size(values)]:random());
        alignas(16) std::array<std::uint8_t,16> left{};
        const auto low=variant<16?values[(variant+3)%std::size(values)]:random(),high=variant<16?values[(variant+7)%std::size(values)]:random();
        f::detail::write_word(left.data(),low,8);f::detail::write_word(left.data()+8,high,8);
        auto right=left;auto native=portable;std::uint32_t flags=0x202|(unsigned(random())&wr::STATUS_FLAGS),native_flags=flags;
        c.native(native.bytes.data(),right.data(),&native_flags);c.portable(portable.bytes.data(),left.data(),&flags);
        const bool same=portable.get32(24)==native.get32(24) && left==right && ((flags^native_flags)&wr::STATUS_FLAGS)==0 &&
            std::equal(portable.bytes.begin()+160,portable.bytes.begin()+288,native.bytes.begin()+160);
        ++total;
        if(!same){
            const auto group=std::to_string(c.prefix)+'/'+std::to_string(c.opcode)+'/'+std::to_string(c.memory);
            if(by_form[group]++==0){
                const auto reg=c.memory?c.encoding:c.encoding/8;
                differences<<c.name<<" rc="<<rounding<<" mode="<<mode<<" variant="<<variant<<"\n portable "<<state(portable,reg)<<"\n native   "<<state(native,reg)
                    <<"\n operand "<<std::hex<<f::detail::read_word(left.data(),8)<<'/'<<f::detail::read_word(right.data(),8)
                    <<" flags "<<flags<<'/'<<native_flags<<std::dec<<'\n';
            }
            ++failures;
        }
    }
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.portable-simd-kernel.v1\",\"forms\":"<<simd_kernel_cases().size()<<",\"cases\":"<<total<<",\"failures\":"<<failures
        <<",\"source_executable_opened\":false,\"screen_access\":false,\"game_launched\":false,\"passed\":"<<(failures?"false":"true")<<"}\n";
    if(failures){std::cerr<<differences.str();for(const auto& [name,count]:by_form)std::cerr<<name<<' '<<count<<'\n';std::cerr<<failures<<" / "<<total<<" SIMD mismatches\n";return 1;}
    std::cout<<"PASS portable SIMD "<<total<<" cases\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
