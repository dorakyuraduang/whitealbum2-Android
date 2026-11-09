// Controlled clocks/counter strings only. No SDL initialization or game UI.
#include "../platform/window/event_pump.hpp"
#include "../platform/compat/diagnostic_chunks.hpp"
#include "../platform/render/frame_metrics.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check at ")+std::to_string(__LINE__)+": "+#x);}while(false)
int main(int argc,char** argv){try{
    CHECK(argc==2);wa2::window::EventPump pump;unsigned pumps{},coalesced{},guards{},transports{};
    for(unsigned burst=0;burst<12;++burst)for(unsigned query=0;query<256;++query){const bool due=pump.due(0,10000000ull*burst);pumps+=due;coalesced+=!due;CHECK(due==(query==0));}
    CHECK(pumps==12 && coalesced==3060);
    CHECK(!pump.due(0,110999999) && pump.due(0,111000000));++guards;
    for(unsigned n=0;n<4;++n)CHECK(pump.due(16,111000000));++guards;
    pump.reset();CHECK(pump.due(0,111000000) && pump.due(0,1));++guards;
    CHECK(pump.due(0,std::numeric_limits<std::uint64_t>::max()-1));CHECK(!pump.due(0,std::numeric_limits<std::uint64_t>::max()));CHECK(pump.due(0,0));++guards;
    CHECK(pump.report().find("\"blocking_pumps\":4")!=std::string::npos);
    const auto transport=[&](const std::string& text){std::vector<std::pair<std::string,std::string>> parts;
        wa2::platform::compat::emit_counter_record("fixture #1 native_resources",text,[&](auto label,auto body){parts.emplace_back(label,body);});
        std::string joined;for(unsigned n=0;n<parts.size();++n){CHECK(parts[n].second.size()<=640 && parts[n].first.size()+parts[n].second.size()+1<1024);
            if(parts.size()>1)CHECK(parts[n].first=="fixture #1 native_resources part "+std::to_string(n+1)+"/"+std::to_string(parts.size()));
            if(!parts[n].second.empty())CHECK((static_cast<unsigned char>(parts[n].second[0])&0xc0)!=0x80);joined+=parts[n].second;}
        CHECK(joined==text);++transports;
    };
    transport("{}");transport(std::string(640,'x'));transport(std::string(65536,'x'));
    std::string unicode(638,'x');for(unsigned n=0;n<1000;++n)unicode+="\xe7\x99\xbd";transport(unicode);
    for(unsigned kind=0;kind<3;++kind){unsigned calls{};bool rejected{};
        try{wa2::platform::compat::emit_counter_record(std::string(kind==0?129:10,'x'),std::string(kind==1?65537:700,kind==2?char(0x80):'x'),[&](auto,auto){++calls;});}catch(const std::exception&){rejected=true;}
        CHECK(rejected && !calls);++guards;
    }
    wa2::graphics::FrameMetrics frames;frames.draw=3;frames.upload=7;
    std::array<std::uint64_t,10> work{1,2,3,4,5,6,7,8,9,10};frames.presented(1,90,93,95,100,2,4,5,work);
    frames.draw=11;frames.upload=13;work[9]=27;frames.presented(2,110,114,116,120,5,9,8,work);
    const auto summary=frames.summary();CHECK(summary.find("\"frames\":2")!=std::string::npos && summary.find("\"interval_total_ns\":20")!=std::string::npos);
    CHECK(summary.find("\"stage_total_ns\":[14,0,0,20,5,4,9]")!=std::string::npos && summary.find("\"last_present_work\":[1,2,3,4,5,6,7,8,9,27]")!=std::string::npos);
    CHECK(summary.find("recent")==std::string::npos && summary.find("\"screen_capture\":false")!=std::string::npos);++guards;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.host-progress.v1\",\"burst_queries\":3072,\"device_pumps\":"<<pumps<<",\"coalesced_queries\":"<<coalesced<<",\"boundary_cases\":"<<guards<<",\"transport_roundtrips\":"<<transports<<",\"maximum_input_coalescing_ns\":1000000,\"blocking_waits_bypass\":true,\"frame_summary_exact\":true,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out.good());
    std::cout<<"PASS event cadence, bounded UTF-8 counter transport and frame-work summary.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
