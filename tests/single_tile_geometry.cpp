// Exhaustive finite-grid encoding against independent hardware arithmetic.
// No game, original binary, resource, runtime, graphics or OS device.
#include "../engine/single_tile_geometry.hpp"
#include <bit>
#include <cfenv>
#include <cerrno>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#pragma STDC FENV_ACCESS ON
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
namespace g=wa2::geometry;
int main(int argc,char** argv){try{
    CHECK(argc==2);CHECK(!std::fesetenv(FE_DFL_ENV));
    std::uint64_t coordinates{},ratios{};unsigned rejected{},environment_cases{};
    for(std::int32_t n=-8388608;n<=8388608;++n){
        const auto reference=std::bit_cast<std::uint32_t>(float(n)/256.f);
        const auto encoded=g::dyadic_bits(n,8);CHECK(encoded==reference);
        const auto decoded=g::fixed_coordinate(reference);CHECK(decoded && *decoded==n);++coordinates;
    }
    for(unsigned denominator=0;denominator<=23;++denominator)for(std::int32_t n=0;n<=4096;++n){
        const auto expected=std::bit_cast<std::uint32_t>(std::ldexp(float(n),-int(denominator)));
        CHECK(g::dyadic_bits(n,denominator)==expected);++ratios;
    }
    for(auto word:std::array<std::uint32_t,16>{0x80000000,0x7f800000,0xff800000,0x7fc00000,0xffc00000,0x7f800001,0xff800001,
        1,0x80000001,0x007fffff,0x807fffff,0x47000001,0xc7000001,0x3b000000,0xbb000000,0x3f800001}){
        CHECK(!g::fixed_coordinate(word));++rejected;
    }
    struct Point {std::uint32_t bits;std::int32_t value;};std::vector<Point> samples;
    for(std::int32_t n=-8388608;n<=8388608;n+=4096)samples.push_back({std::bit_cast<std::uint32_t>(float(n)/256.f),n});
    for(int rounding:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){
        CHECK(!std::fesetround(rounding));std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO|FE_INEXACT);errno=ERANGE;
        const auto raised=std::fetestexcept(FE_ALL_EXCEPT);
        for(const auto& sample:samples){const auto value=g::fixed_coordinate(sample.bits);CHECK(value && *value==sample.value);
            CHECK(g::dyadic_bits(*value,8)==sample.bits);}
        CHECK(std::fegetround()==rounding && std::fetestexcept(FE_ALL_EXCEPT)==raised && errno==ERANGE);++environment_cases;
    }
    CHECK(!std::fesetenv(FE_DFL_ENV));
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.exact-single-tile-geometry.v1\",\"exact_hardware_coordinates\":"<<coordinates
        <<",\"exact_hardware_ratios\":"<<ratios<<",\"rejected_coordinates\":"<<rejected<<",\"host_environment_cases\":"<<environment_cases
        <<",\"input_quantization\":false,\"game_launched\":false,\"screen_access\":false,\"graphics_device_opened\":false,\"passed\":true}\n";
    CHECK(out.good());std::cout<<"PASS exact coordinate encoding: "<<coordinates<<" hardware comparisons, preserved host environment.\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
