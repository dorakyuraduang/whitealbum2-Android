#include "../platform/render/stream_buffer.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("stream check at ")+std::to_string(__LINE__)+": "+#x);}while(false)
using Stream=wa2::graphics::StreamBuffer<128>;
int main(int argc,char** argv){try{
    CHECK(argc==2);Stream stream;unsigned allocations{},uploads{},direct{},external{},zero{};
    struct Draw {std::shared_ptr<std::vector<std::uint8_t>> storage;std::size_t offset;std::vector<std::uint8_t> expected;};
    std::shared_ptr<std::vector<std::uint8_t>> storage;std::vector<Draw> queued;
    for(unsigned n=0;n<2048;++n){
        if(n && n%17==0){storage=std::make_shared<std::vector<std::uint8_t>>(37,0xfe);stream.invalidate();++external;}
        const std::array<unsigned,9> sizes{0,1,7,16,33,64,127,128,129};const auto size=sizes[n%sizes.size()];
        auto plan=stream.reserve(size);std::vector<std::uint8_t> bytes(size);
        for(unsigned j=0;j<size;++j)bytes[j]=std::uint8_t(n*13+j*31);
        if(!size){CHECK(!plan.allocation && !plan.direct);++zero;continue;}
        if(plan.direct){CHECK(plan.offset==0 && plan.allocation==size && size>Stream::MaximumStorage);storage=std::make_shared<std::vector<std::uint8_t>>(size);++direct;}
        else{CHECK(plan.offset%16==0);if(plan.allocation){CHECK(plan.allocation==Stream::MaximumStorage);storage=std::make_shared<std::vector<std::uint8_t>>(plan.allocation);++allocations;}}
        CHECK(storage && plan.offset+size<=storage->size());std::copy(bytes.begin(),bytes.end(),storage->begin()+plan.offset);
        queued.push_back({storage,plan.offset,bytes});++uploads;
        // Model deferred GPU consumption: every earlier draw must still see
        // its original bytes after wraps, oversized and unrelated uploads.
        for(const auto& draw:queued)CHECK(std::equal(draw.expected.begin(),draw.expected.end(),draw.storage->begin()+draw.offset));
    }
    const auto maximum=stream.reserve(std::size_t(-1));CHECK(maximum.direct && maximum.allocation==std::size_t(-1));
    const auto fresh=stream.reserve(16);CHECK(fresh.allocation==128 && !fresh.offset && !fresh.direct);
    std::ofstream report(argv[1]);report<<"{\"uploads\":"<<uploads<<",\"allocations\":"<<allocations<<",\"direct\":"<<direct<<",\"external_invalidations\":"<<external<<",\"zero_packets\":"<<zero
        <<",\"all_queued_bytes_preserved\":true,\"wrap_and_large_packet_checked\":true,\"alignment\":16,\"maximum_game_stream_storage\":"<<(wa2::graphics::StreamBuffer<512*1024>::MaximumStorage+wa2::graphics::StreamBuffer<128*1024>::MaximumStorage)
        <<",\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(report);
    std::cout<<"PASS bounded stream offsets, queued bytes, wraps and external uploads.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
