// Bounded arithmetic/serialization checks. No frame loop, game, or devices.
#include "floating.hpp"
#include <bit>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
extern "C" void wr_x87_217_m0(void*,void*,std::uint32_t*);
extern "C" void wr_x87_217_m3(void*,void*,std::uint32_t*);
extern "C" void wr_x87_216_m0(void*,void*,std::uint32_t*);
extern "C" void wr_x87_216_m1(void*,void*,std::uint32_t*);
extern "C" void wr_x87_216_m6(void*,void*,std::uint32_t*);
extern "C" void wr_x87_222_r233(void*,void*,std::uint32_t*);
namespace f=wa2::aot::fp;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
template<bool Native> std::uint32_t transform(wr::Cpu& cpu,unsigned variant){
    std::uint32_t input=std::bit_cast<std::uint32_t>(float(int(variant%63)-31)*.125f),
        other=std::bit_cast<std::uint32_t>(float(variant%37)*.03125f),
        scale=0x3f400001,rotation=0x3eaaaaab,origin=0x43201234,result{};
    const auto op=[&]<auto Kernel,wr::X87Native Oracle>(void* operand,unsigned opcode){
        cpu.eip+=3;if constexpr(Native)wr::x87_execute(cpu,Oracle,operand,opcode);
        else f::x87_execute<Kernel>(cpu,operand,opcode);
    };
    op.template operator()<f::x87<217,0,true>,wr_x87_217_m0>(&input,0x100);
    op.template operator()<f::x87<216,1,true>,wr_x87_216_m1>(&scale,0x108);
    op.template operator()<f::x87<217,0,true>,wr_x87_217_m0>(&other,0x100);
    op.template operator()<f::x87<216,1,true>,wr_x87_216_m1>(&rotation,0x108);
    op.template operator()<f::x87<222,233,false>,wr_x87_222_r233>(nullptr,0x6e9);
    op.template operator()<f::x87<216,0,true>,wr_x87_216_m0>(&origin,0x100);
    op.template operator()<f::x87<217,3,true>,wr_x87_217_m3>(&result,0x118);
    return result;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned exact{},serialization{},divisions{};
    for(unsigned offset=0;offset<=513;++offset)for(unsigned width:{2u,4u,8u}){
        wr::FpState actual,expected;for(unsigned n=0;n<512;++n)actual.bytes[n]=expected.bytes[n]=std::uint8_t(n*17+3);
        const std::uint64_t bits=0x123456789abcdef0ull;bool a_fault{},b_fault{};
        try{if(width==2)actual.put16(offset,std::uint16_t(bits));else if(width==4)actual.put32(offset,std::uint32_t(bits));else actual.put64(offset,bits);}catch(const std::out_of_range&){a_fault=true;}
        try{for(unsigned n=0;n<width;++n)expected.bytes.at(offset+n)=std::uint8_t(bits>>(n*8));}catch(const std::out_of_range&){b_fault=true;}
        CHECK(a_fault==b_fault && actual.bytes==expected.bytes);++serialization;
        a_fault=b_fault=false;std::uint64_t a{},b{};
        try{a=width==2?actual.get16(offset):width==4?actual.get32(offset):actual.get64(offset);}catch(const std::out_of_range&){a_fault=true;}
        try{for(unsigned n=0;n<width;++n)b|=std::uint64_t(expected.bytes.at(offset+n))<<(n*8);}catch(const std::out_of_range&){b_fault=true;}
        CHECK(a_fault==b_fault && (a_fault || a==b));++serialization;
    }
    for(unsigned precision:{0u,2u,3u})for(unsigned rounding=0;rounding<4;++rounding)for(unsigned variant=0;variant<32;++variant){
        wr::Cpu actual;actual.eip=0x43bc0d;actual.fp.put16(0,std::uint16_t(0x7f|(precision<<8)|(rounding<<10)));auto expected=actual;
        CHECK(transform<false>(actual,variant)==transform<true>(expected,variant));CHECK(actual.fp.bytes==expected.fp.bytes);++exact;
    }
    std::mt19937 random(0x243f6a88);
    for(unsigned rounding=0;rounding<4;++rounding)for(unsigned variant=0;variant<256;++variant){
        const std::uint32_t numerator=(random()&0x807fffff)|((1+random()%254)<<23),denominator=(random()&0x807fffff)|((1+random()%254)<<23);
        wr::Cpu actual;actual.eip=0x43ce23;actual.fp.put16(0,std::uint16_t(0x7f|(rounding<<10)));
        const unsigned top=variant%8;actual.fp.put16(2,std::uint16_t(top<<11));actual.fp.bytes[4]=std::uint8_t(1u<<top);
        f::Environment environment;const auto value=f::from_binary(numerator,32,environment);
        actual.fp.put64(32,value.significand);actual.fp.put16(40,value.sign_exponent);auto expected=actual;auto operand=denominator;
        f::x87_execute<f::x87<216,6,true>>(actual,&operand,0x130);
        wr::x87_execute(expected,wr_x87_216_m6,&operand,0x130);
        CHECK(actual.fp.bytes==expected.fp.bytes);++divisions;
    }
    std::uint64_t ns{},checksum{};constexpr unsigned rounds=5,sequences=512;
    for(unsigned round=0;round<rounds;++round){wr::Cpu cpu;cpu.eip=0x43bc0d;cpu.fp.put16(0,0x7f);
        const auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<sequences;++i)checksum=std::rotl(checksum,1)^transform<false>(cpu,i+round*sequences);
        ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    }
    std::ofstream report(argv[1]);report<<"{\"schema\":\"wa2.bounded-fp-work.v1\",\"serialization_cases\":"<<serialization<<",\"hardware_oracle_sequences\":"<<exact
        <<",\"arithmetic_sequences\":"<<rounds*sequences<<",\"arithmetic_operations\":"<<rounds*sequences*7<<",\"elapsed_ns\":"<<ns<<",\"checksum\":"<<checksum
        <<",\"24bit_division_hardware_oracle_cases\":"<<divisions
        <<",\"whole_game_fps_measured\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";
    std::cout<<"PASS exact FP sequences and serialization; bounded arithmetic "<<ns<<" ns.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
