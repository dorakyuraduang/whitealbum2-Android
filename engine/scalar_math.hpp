#pragma once
#include <cstdint>
#include <span>
namespace wa2::math {
struct ScalarResult {double value;std::uint16_t exceptions;};
// Ordinary native math. The exception bits use IEEE invalid/divide/overflow/
// underflow/inexact positions 0/2/3/4/5, independent of the host fenv layout.
// Finite inputs and nearest rounding are the admitted source CRT contract.
ScalarResult atan2_finite(double y,double x);
ScalarResult sine_finite(double);
ScalarResult cosine_finite(double);
ScalarResult sine_finite_uncached(double);
ScalarResult cosine_finite_uncached(double);
bool exact_trigonometry_input(double);
std::span<const std::uint64_t> trigonometry_source_words();
// Caller establishes nearest IEEE binary64 and the admitted input domain.
double exact_trigonometry(double,bool cosine);
struct TrigonometryCacheStats {std::uint64_t hits{},misses{};};
TrigonometryCacheStats trigonometry_cache_stats();
}
