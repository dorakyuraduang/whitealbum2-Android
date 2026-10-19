#include "scalar_math.hpp"
#include "math_environment.hpp"
#include <cmath>
#include <array>
#pragma STDC FENV_ACCESS ON
namespace wa2::math {
ScalarResult sine_finite_uncached(double value){
    if(!detail::finite(value))throw std::invalid_argument("Native sine requires a finite source argument");
    detail::NativeEnvironment scope;return scope.finish(exact_trigonometry_input(value)?exact_trigonometry(value,false):std::sin(value));
}
ScalarResult cosine_finite_uncached(double value){
    if(!detail::finite(value))throw std::invalid_argument("Native cosine requires a finite source argument");
    detail::NativeEnvironment scope;return scope.finish(exact_trigonometry_input(value)?exact_trigonometry(value,true):std::cos(value));
}
namespace {
struct CacheEntry {std::uint64_t key{};ScalarResult result{};bool valid{};};
thread_local std::array<CacheEntry,64> sine_cache,cosine_cache;
thread_local TrigonometryCacheStats statistics;
template<auto Calculate>ScalarResult cached(double value,std::array<CacheEntry,64>& entries){
    if(!detail::finite(value))throw std::invalid_argument("Native trigonometry requires a finite source argument");
    const auto key=std::bit_cast<std::uint64_t>(value);
    const auto slot=((key^(key>>32))*0x9e3779b97f4a7c15ull)>>58;
    auto& entry=entries[slot];
    if(entry.valid && entry.key==key){++statistics.hits;return entry.result;}
    const auto result=Calculate(value);entry={key,result,true};++statistics.misses;return result;
}
}
// A repeated angle has the same nearest-rounded value and exception bits.
// Hits preserve the caller's fenv/errno without entering the native CRT again.
ScalarResult sine_finite(double value){return cached<sine_finite_uncached>(value,sine_cache);}
ScalarResult cosine_finite(double value){return cached<cosine_finite_uncached>(value,cosine_cache);}
TrigonometryCacheStats trigonometry_cache_stats(){return statistics;}
}
