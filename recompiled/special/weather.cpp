#include "../weather_kernel.hpp"
#include "weather_profile.hpp"
namespace wa2::special {namespace {
using namespace aot::weather_detail;
#include "weather_batch.inc"
}
bool update_weather(wr::Cpu& cpu,wr::Memory& memory){
    return aot::weather_detail::population(memory,weather_source,&cpu) &&
        aot::weather_detail::batch(cpu,memory,weather_source,weather_408507);
}
std::string weather_report(){
    const auto c=aot::weather_batch_counts;
    return "{\"attempts\":"+std::to_string(c.attempts)+",\"completed\":"+std::to_string(c.completed)+
        ",\"yields\":"+std::to_string(c.yields)+",\"declined\":"+std::to_string(c.declined)+
        ",\"shared_main_kernel\":true,\"maximum_particles_per_bank\":400}";
}
}
