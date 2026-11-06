// Constructed vertices only: no window, renderer, game or original resources.
#include "scene_geometry.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>

namespace i=wa2::image;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("check failed: ")+#x);}while(false)
bool near(float a,float b){return std::abs(a-b)<.0001f;}
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}CHECK(caught);}
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};
    i::QuadGeometry q{0,0,64,64,0,0,64,64};
    auto v=i::scene_quad(q,64,64);CHECK(v && (*v)[0].x==0 && (*v)[1].x==64 && (*v)[2].y==64 && (*v)[2].u==1);++cases;
    // Cached memory-only sprite oracle has X=15.5..63.5 for U=-16..48.
    // SDL pixel edges remove its D3D9 -0.5 offset. Logical image pixels keep
    // the same interpolation; unused physical texture padding is omitted.
    q.source_x=-16;v=i::scene_quad(q,64,64);CHECK(v && (*v)[0].x==16 && (*v)[1].x==64 && (*v)[0].u==0 && (*v)[1].u==.75f);++cases;
    q.source_x=-1;v=i::scene_quad(q,64,64);CHECK(v && (*v)[0].x==1 && (*v)[1].u==63.f/64);++cases;
    q.source_x=16;v=i::scene_quad(q,64,64);CHECK(v && (*v)[0].x==0 && (*v)[1].x==48 && (*v)[0].u==.25f && (*v)[1].u==1);++cases;
    q.source_x=64;CHECK(!i::scene_quad(q,64,64));q.source_x=-65;CHECK(!i::scene_quad(q,64,64));++cases;
    q.source_x=0;q.source_width=128;v=i::scene_quad(q,64,64);CHECK(v && (*v)[1].x==32 && (*v)[1].u==1);++cases;
    q.source_width=64;q.source_y=-16;q.source_height=96;v=i::scene_quad(q,64,64);
    CHECK(v && near((*v)[0].y,64.f/6) && near((*v)[2].y,64.f*5/6) && (*v)[0].v==0 && (*v)[2].v==1);++cases;
    q={0,0,64,64,0,0,64,64,90};v=i::scene_quad(q,64,64);
    CHECK(v && near((*v)[1].x,-.00000279753f) && near((*v)[1].y,-64) && near((*v)[5].x,64));++cases;
    // Off-centre reflection is a position transform, not just exchanged UVs.
    q={10,20,30,40,0,0,64,64,0,7,13,1,1,1};v=i::scene_quad(q,64,64);
    CHECK(v && (*v)[0].x==4 && (*v)[1].x==-26 && (*v)[0].u==0 && (*v)[1].u==1 && (*v)[0].y==20);++cases;
    for(unsigned flip=0;flip<4;++flip)for(int angle:{0,30,90,180,270}){
        q={10,20,30,40,0,0,64,64,float(angle),7,13,2,3,flip};v=i::scene_quad(q,64,64);CHECK(v);
        const auto phase=float(double(angle)*.01745329238474369);const auto c=std::cos(phase),s=std::sin(phase);
        unsigned n{};for(const auto& xy:std::array<std::array<float,2>,6>{{{10,20},{40,20},{40,60},{10,20},{40,60},{10,60}}}){
            const auto dx=(xy[0]-7)*(flip&1?-1:1),dy=(xy[1]-13)*(flip&2?-1:1);
            CHECK(near((*v)[n].x,7+(c*dx+s*dy)*2) && near((*v)[n].y,13+(c*dy-s*dx)*3));++n;
        }++cases;
    }
    // Clip fractions are applied before reflection/rotation/nonuniform scale.
    q={0,0,64,64,-16,0,64,64,90,0,0,2,3,1};v=i::scene_quad(q,64,64);
    CHECK(v && near((*v)[0].x,0) && near((*v)[0].y,48) && near((*v)[2].x,128) && near((*v)[2].y,192));++cases;
    q={0,0,64,64,0,0,64,64};for(unsigned axis=0;axis<3;++axis){auto zero=q;if(axis==0)zero.width=0;else if(axis==1)zero.source_height=0;else zero.scale_x=0;CHECK(!i::scene_quad(zero,64,64));++cases;}
    for(auto bad:std::array<i::QuadGeometry,5>{{{0,0,-1,64,0,0,64,64},{0,0,64,64,0,0,-1,64},
        {0,0,64,64,0,0,64,64,0,0,0,-1,1},{0,0,64,64,0,0,64,64,0,0,0,1,1,4},
        {std::numeric_limits<float>::quiet_NaN(),0,64,64,0,0,64,64}}}){rejects([&]{i::scene_quad(bad,64,64);});++guards;}
    rejects([&]{i::scene_quad(q,0,64);});rejects([&]{i::scene_quad(q,64,65536);});guards+=2;
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-scene-geometry.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"synthetic_only\":true,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";out.close();CHECK(out);
    std::cout<<"PASS native quad source clipping, reflection and transform order.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
