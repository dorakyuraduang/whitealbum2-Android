// Exact bounds/unaligned/tail checks of the shared integer equality kernels.
#include "../engine/byte_equality.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <string>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace m=wa2::memory;
template<unsigned N>unsigned fixed(){unsigned cases{};
    for(unsigned offset=0;offset<16;++offset){std::array<std::uint8_t,N+32> a,b;a.fill(0x55);b.fill(0xaa);auto* x=a.data()+offset;auto* y=b.data()+15-offset;
        for(unsigned n=0;n<N;++n)x[n]=y[n]=std::uint8_t(n*73+19);
        CHECK(m::equal_bytes<N>(x,y));++cases;
        for(unsigned n=0;n<N;++n){y[n]^=std::uint8_t(1u<<(n&7));CHECK(!m::equal_bytes<N>(x,y));y[n]=x[n];++cases;}
    }return cases;
}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned constant=fixed<0>()+fixed<1>()+fixed<4>()+fixed<7>()+fixed<8>()+fixed<24>()+fixed<128>()+fixed<160>()+fixed<256>();
    unsigned dynamic{},words{};std::array<std::uint8_t,1056> a,b;
    for(unsigned size=0;size<=1024;++size)for(unsigned offset:{0u,1u,7u,15u}){a.fill(0x55);b.fill(0xaa);
        auto x=std::span(a).subspan(offset,size),y=std::span(b).subspan(15-offset,size);for(unsigned n=0;n<size;++n)x[n]=y[n]=std::uint8_t(n*71+3);
        CHECK(m::equal_bytes(x,y));++dynamic;
        if(size)for(unsigned at:{0u,size/2,size-1}){y[at]^=1;CHECK(!m::equal_bytes(x,y));y[at]=x[at];++dynamic;}
        CHECK(!m::equal_bytes(x,std::span(b).first(size+1)));++dynamic;
    }
    for(unsigned size=0;size<=256;++size){std::array<std::uint32_t,257> x,y;
        for(unsigned n=0;n<257;++n)x[n]=y[n]=n*0x9e3779b9u;y[size]^=0xffffffffu;
        CHECK(m::equal_words(std::span(x).first(size),std::span(y).first(size)));++words;
        if(size)for(unsigned at:{0u,size/2,size-1}){y[at]^=1;CHECK(!m::equal_words(std::span(x).first(size),std::span(y).first(size)));y[at]=x[at];++words;}
        CHECK(!m::equal_words(std::span(x).first(size),std::span(y).first(size+1)));++words;
    }
    CHECK(m::equal_bytes({},{}));CHECK(m::equal_words({},{}));CHECK(m::equal_bytes<0>(nullptr,nullptr));
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.byte-equality.v1\",\"fixed_cases\":"<<constant<<",\"dynamic_cases\":"<<dynamic<<",\"word_cases\":"<<words
        <<",\"unaligned_and_exact_tails\":true,\"outside_bytes_do_not_affect_result\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out.good());
    std::cout<<"PASS bounded byte equality: "<<constant<<" fixed, "<<dynamic<<" dynamic, "<<words<<" word cases.\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
