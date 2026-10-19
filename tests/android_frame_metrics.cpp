#include "frame_metrics.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace wa2::android;
#define CHECK(value) do{if(!(value))throw std::runtime_error(#value);}while(false)
void close(double value,double expected,double epsilon=0.00001){CHECK(std::abs(value-expected)<epsilon);}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases=0;
    for(int rate:{30,60,90,120,144,240}){
        FrameMetrics metrics;unsigned updates=0;
        for(int frame=0;frame<=rate*2;++frame){
            if(const auto sample=metrics.presented(std::int64_t(frame)*1000000000/rate)){
                close(sample->fps,rate,0.0001);close(sample->average_ms,1000.0/rate,0.00001);
                CHECK(sample->presented==std::uint64_t(frame+1));
                CHECK(sample->p95_ms>=sample->average_ms-0.00001);++updates;
            }
        }
        CHECK(updates>=7);++cases;
    }
    {
        FrameMetrics metrics;CHECK(!metrics.presented(0));
        for(int frame=1;frame<=20;++frame)CHECK(!metrics.presented(frame*8000000ll));
        const auto stalled=metrics.presented(260000000);CHECK(stalled);
        close(stalled->fps,21.0/0.26);close(stalled->maximum_ms,100);close(stalled->p95_ms,8);++cases;
    }
    {
        FrameMetrics metrics;CHECK(!metrics.presented(0));
        CHECK(metrics.presented(1000000000)); // A real stall remains visible.
        metrics.reset();CHECK(!metrics.presented(500000000000ll));
        const auto resumed=metrics.presented(500250000000ll);CHECK(resumed);
        close(resumed->fps,4);CHECK(resumed->presented==2);++cases;
    }
    {
        FrameMetrics metrics;CHECK(!metrics.presented(100));CHECK(!metrics.presented(50));
        const auto recovered=metrics.presented(250000050);CHECK(recovered);
        close(recovered->fps,4);CHECK(recovered->presented==2);++cases;
    }
    {
        FrameMetrics metrics;CHECK(!metrics.presented(-1));CHECK(!metrics.presented(0));
        for(int frame=1;frame<=2500;++frame){
            const auto sample=metrics.presented(frame*100000ll);
            if(frame<2500)CHECK(!sample);
            else{CHECK(sample);close(sample->fps,10000);close(sample->average_ms,0.1);
                close(sample->p95_ms,0.1);CHECK(sample->percentile_samples==FrameMetrics::capacity);}
        }
        ++cases;
    }
    {
        FrameMetrics metrics;CHECK(!metrics.presented(0));
        for(int i=0;i<5;++i)CHECK(!metrics.presented(0));
        const auto sample=metrics.presented(250000000);CHECK(sample);
        CHECK(std::isfinite(sample->fps));close(sample->fps,24);++cases;
    }
    {
        FrameMetrics metrics(0);CHECK(!metrics.presented(0));
        const auto sample=metrics.presented(1);CHECK(sample);close(sample->fps,1e9);++cases;
    }
    {
        FrameMetrics metrics;const auto start=std::numeric_limits<std::int64_t>::max()-1000000000;
        CHECK(!metrics.presented(start));const auto sample=metrics.presented(start+250000000);CHECK(sample);
        close(sample->fps,4);++cases;
    }
    {
        FrameMetrics metrics;CHECK(!metrics.presented(0));
        // Without successful presents the observer cannot invent new frames.
        metrics.reset();CHECK(!metrics.presented(1000000000));++cases;
    }
    std::ofstream output(argv[1]);CHECK(bool(output));
    output<<"{\"schema\":\"wa2.android-frame-metrics.v1\",\"cases\":"<<cases
        <<",\"real_time_waits\":false,\"game_launched\":false,\"screen_access\":false,\"bounded_samples\":"<<FrameMetrics::capacity
        <<",\"android_device_verified\":false,\"passed\":true}\n";
    std::cout<<"Passed "<<cases<<" synthetic frame-statistics cases; no game or pressure test was run.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
