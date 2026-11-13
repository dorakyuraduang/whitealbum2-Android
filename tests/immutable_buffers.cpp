#include "../platform/render/immutable_buffers.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("immutable check at ")+std::to_string(__LINE__)+": "+#x);}while(false)
struct Collision {std::uint64_t operator()(auto,auto)const{return 7;}};
int main(int argc,char** argv){try{
    CHECK(argc==2);using Bytes=std::vector<std::uint8_t>;
    struct Stored {Bytes vertices,indices;};using Ref=std::shared_ptr<Stored>;
    unsigned next{},created{},released{},draws{};std::map<unsigned,Ref> driver;std::vector<Ref> queued;std::vector<Stored> expected;
    const auto create=[&](auto vertices,auto indices)->std::optional<unsigned>{auto value=std::make_shared<Stored>();value->vertices.assign(vertices.begin(),vertices.end());value->indices.assign(indices.begin(),indices.end());driver[++next]=value;++created;return next;};
    const auto release=[&](unsigned handle){CHECK(driver.erase(handle)==1);++released;};
    wa2::graphics::ImmutableBuffers<unsigned,4,256,96,Collision> cache;
    for(unsigned n=0;n<512;++n){Bytes v(32),i(12);for(unsigned x=0;x<v.size();++x)v[x]=std::uint8_t((n/3)%9*19+x);for(unsigned x=0;x<i.size();++x)i[x]=std::uint8_t(x*3);
        auto handle=cache.acquire(v,i,create,release);
        if(handle){auto current=driver.at(*handle);CHECK(current->vertices==v && current->indices==i);queued.push_back(current);expected.push_back({v,i});++draws;}
        for(unsigned x=0;x<queued.size();++x)CHECK(queued[x]->vertices==expected[x].vertices && queued[x]->indices==expected[x].indices);
        CHECK(cache.size()<=4 && cache.bytes()<=256 && driver.size()==cache.size());
        if(n%61==60)cache.clear(release);
    }
    CHECK(cache.hits>100 && cache.evictions>20);cache.clear(release);CHECK(driver.empty() && !cache.size() && !cache.bytes());
    wa2::graphics::ImmutableBuffers<unsigned,4,256,96> normal;Bytes v(32,1),i(12,2);const auto before=created;
    CHECK(!normal.acquire(v,i,create,release));CHECK(created==before);
    CHECK(normal.acquire(v,i,create,release));CHECK(created==before+1);
    CHECK(normal.acquire(v,i,create,release));CHECK(created==before+1);
    const auto used=normal.bytes();Bytes oversized(97,3);
    CHECK(!normal.acquire(oversized,i,create,release));CHECK(normal.bytes()==used);
    CHECK(!normal.acquire({},i,create,release));normal.clear(release);
    auto fail=[](auto,auto)->std::optional<unsigned>{return {};};
    CHECK(!normal.acquire(v,i,fail,release));CHECK(!normal.acquire(v,i,fail,release));CHECK(!normal.size());
    CHECK(normal.acquire(v,i,create,release));normal.clear(release);CHECK(created==released && driver.empty());
    std::ofstream out(argv[1]);out<<"{\"packets\":512,\"queued_draws\":"<<draws<<",\"collisions_checked\":true,\"all_retained_bytes_unchanged\":true,\"two_use_admission\":true,\"bounded_eviction_and_release\":true,\"allocation_retry\":true,\"production_maximum_payload_bytes\":"<<wa2::graphics::ImmutableBuffers<unsigned>::maximum_bytes<<",\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);
    std::cout<<"PASS immutable payload reuse, collisions, bounded eviction and pending draw bytes.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
