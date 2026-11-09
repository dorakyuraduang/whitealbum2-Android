#include "image_chroma.hpp"
#include <stdexcept>
namespace wa2::image {namespace {
template<unsigned Step,bool Red,bool Grey>void convert(std::uint8_t* data,unsigned count,const ChromaTables* tables){
    for(unsigned n=0;n<count;++n){auto* pixel=data+std::size_t(n)*Step;const unsigned a=pixel[0],b=pixel[1],c=pixel[2];
        const unsigned grey=(a*(Red?77:28)+b*151+c*(Red?28:77))>>8;
        if constexpr(Grey)pixel[0]=pixel[1]=pixel[2]=std::uint8_t(grey);
        else{const auto contribution=tables->grey[grey];
            pixel[2]=std::uint8_t(tables->channel[c]+contribution);
            pixel[1]=std::uint8_t(tables->channel[b]+contribution);
            pixel[0]=std::uint8_t(tables->channel[a]+contribution);
        }
    }
}
template<unsigned Step,bool Red>void select(std::uint8_t* data,unsigned count,const ChromaTables* tables){
    if(tables)convert<Step,Red,false>(data,count,tables);else convert<Step,Red,true>(data,count,tables);
}
}
void convert_chroma(std::span<std::uint8_t> bytes,unsigned count,unsigned stride,bool red,const ChromaTables* tables){
    if((stride!=3 && stride!=4) || (count && std::uint64_t(count)*stride-(stride-3)>bytes.size()))throw std::invalid_argument("Invalid chroma pixel span");
    if(stride==3){if(red)select<3,true>(bytes.data(),count,tables);else select<3,false>(bytes.data(),count,tables);}
    else if(red)select<4,true>(bytes.data(),count,tables);else select<4,false>(bytes.data(),count,tables);
}
}
