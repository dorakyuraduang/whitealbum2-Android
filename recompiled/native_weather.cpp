#include "weather_kernel.hpp"
namespace wa2::aot {namespace {
using namespace weather_detail;
#include "../generated/shared-aot/weather_batch.inc"
}
#if WA2_AOT_DIAGNOSTICS
bool native_weather_color_snapshot(wr::Cpu& cpu,wr::Memory& memory){
    if(cpu.eip!=0x43b290 || cpu.r[wr::ESP]<256)return false;auto lock=memory.execution_scope();
    if(!writable_frame(lock,cpu.r[wr::ESP]-256,WeatherFrame::StackBytes) ||
       !writable_frame(lock,main_weather_source.objects+81*256,WeatherFrame::SpriteBytes))return false;
    try{
        static thread_local std::vector<std::uint8_t> bytes(WeatherFrame::SpriteBytes);
        WeatherFrame frame(cpu.r[wr::ESP],lock,bytes,main_weather_source);memory.copy_out(frame.base,frame.stack);memory.copy_out(frame.SpriteBase,frame.sprites);
        auto result=cpu;if(!weather_color_direct(result,frame))return false;
        memory.copy_in(frame.base,frame.stack);memory.copy_in(frame.SpriteBase,frame.sprites);cpu=result;return true;
    }catch(const WeatherDeclined&){return false;}catch(const wr::GuestFault&){return false;}
}
#endif
bool native_weather_population(wr::Memory& memory){return weather_detail::population(memory,main_weather_source,Program::current_context(memory));}
bool native_weather_batch(wr::Cpu& cpu,wr::Memory& memory){
#if WA2_AOT_DIAGNOSTICS
    if(!native_render_math_enabled)return false;
#endif
    return weather_detail::batch(cpu,memory,main_weather_source,weather_4088cf);
}
}
