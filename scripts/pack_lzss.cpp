// Offline resource preparation only; never linked into the game application.
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int main(){try{
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    std::vector<std::uint8_t> input((std::istreambuf_iterator<char>(std::cin)),{});
    if(input.empty() || input.size()>256u*1024*1024)throw std::runtime_error("LZSS input must contain 1..256 MiB");
    std::array<int,65536> heads;heads.fill(-1);std::array<int,4096> previous;previous.fill(-1);
    const auto hash=[&](std::size_t at){return (unsigned(input[at])*251+unsigned(input[at+1])*31+input[at+2])&65535;};
    std::vector<std::uint8_t> output(8);output.reserve(input.size()/3);
    for(std::size_t at=0;at<input.size();){
        const auto flag=output.size();output.push_back(0);
        for(unsigned bit=0;bit<8 && at<input.size();++bit){
            unsigned length{};int match=-1;
            if(at+2<input.size()){
                int candidate=heads[hash(at)];const auto limit=std::max(0,int(at)-4096);
                for(unsigned tries=0;tries<32 && candidate>=limit && candidate<int(at);++tries){
                    unsigned size{};while(size<18 && at+size<input.size() && input[candidate+size]==input[at+size])++size;
                    if(size>=3 && size>length){length=size;match=candidate;if(size==18)break;}
                    candidate=previous[unsigned(candidate)&4095];
                }
            }
            if(length>=3){const auto offset=(0xfeeu + unsigned(match))&4095;output.push_back(std::uint8_t(offset));output.push_back(std::uint8_t(((offset>>4)&0xf0)|(length-3)));}
            else{length=1;output[flag]|=std::uint8_t(1u<<bit);output.push_back(input[at]);}
            for(unsigned n=0;n<length;++n){const auto pos=at+n;if(pos+2<input.size()){const auto h=hash(pos);previous[pos&4095]=heads[h];heads[h]=int(pos);}}
            at+=length;
        }
    }
    const auto word=[&](unsigned at,std::uint32_t value){for(unsigned n=0;n<4;++n)output[at+n]=std::uint8_t(value>>(n*8));};
    word(0,std::uint32_t(output.size()));word(4,std::uint32_t(input.size()));
    std::cout.write(reinterpret_cast<const char*>(output.data()),std::streamsize(output.size()));if(!std::cout)throw std::runtime_error("Cannot write LZSS output");return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
