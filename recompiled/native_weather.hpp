#pragma once
namespace wr {struct Cpu;class Memory;}
namespace wa2::aot {
bool native_weather_population(wr::Memory&);
bool native_weather_batch(wr::Cpu&,wr::Memory&);
}
#if WA2_AOT_DIAGNOSTICS
namespace wa2::aot {bool native_weather_color_snapshot(wr::Cpu&,wr::Memory&);}
#endif
