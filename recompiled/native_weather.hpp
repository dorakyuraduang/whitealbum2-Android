#pragma once
#include <cstdint>
namespace wr {struct Cpu;class Memory;}
namespace wa2::aot {
struct WeatherSource {
    std::uint32_t begin,resume,count,objects,weather;
    unsigned required_register;std::uint32_t required_value;
};
inline constexpr WeatherSource main_weather_source{0x4088cf,0x408920,0x5267d4,0xab3fb8,0x522000,5,1};
inline thread_local bool weather_source_reference{};
struct WeatherBatchCounts {std::uint64_t attempts{},completed{},yields{},declined{};};
inline thread_local WeatherBatchCounts weather_batch_counts{};
struct WeatherSourceReferenceScope {
    bool previous=weather_source_reference;
    WeatherSourceReferenceScope(){weather_source_reference=true;}
    ~WeatherSourceReferenceScope(){weather_source_reference=previous;}
};
bool native_weather_population(wr::Memory&);
bool native_weather_batch(wr::Cpu&,wr::Memory&);
}
#if WA2_AOT_DIAGNOSTICS
namespace wa2::aot {bool native_weather_color_snapshot(wr::Cpu&,wr::Memory&);}
#endif
