#include "native_scene_fixture.hpp"
#include <condition_variable>
#include <mutex>

struct Gate:r::Source {
    std::shared_ptr<::Source> bytes;std::uint64_t boundary{};std::mutex mutex;std::condition_variable changed;bool held{true},entered{};
    explicit Gate(std::shared_ptr<::Source> value):bytes(std::move(value)){std::uint64_t entries{};for(unsigned n=0;n<4;++n)entries|=std::uint64_t(bytes->b[12+n])<<(8*n);boundary=16+entries*44;}
    std::uint64_t size()const override{return bytes->size();}
    void read_at(std::uint64_t at,std::span<std::uint8_t> out)override{
        if(at>=boundary){std::unique_lock lock(mutex);if(held){entered=true;changed.notify_all();if(!changed.wait_for(lock,std::chrono::seconds(2),[&]{return !held;}))throw r::Error("constructed image gate timed out");}}
        bytes->read_at(at,out);
    }
    void wait(){std::unique_lock lock(mutex);CHECK(changed.wait_for(lock,std::chrono::seconds(2),[&]{return entered;}));}
    void release(){std::lock_guard lock(mutex);held=false;changed.notify_all();}
};
int main(int argc,char** argv){try{
    CHECK(argc==2);unsigned cases{},guards{};auto gate=std::make_shared<Gate>(bank({{"a.tga",raster(32,16)}}));
    auto store=std::make_unique<i::ImageStore>(r::Archive::open(gate));auto request=store->request("a.tga");gate->wait();CHECK(!request->get());++cases;
    CHECK(!request->wait_for(std::chrono::milliseconds(0)));++cases;CHECK(!request->wait_for(std::chrono::milliseconds(2)));++cases;
    std::array<std::shared_ptr<const i::Raster>,3> results;std::array<std::exception_ptr,3> errors;
    std::array<std::jthread,3> waiters;for(unsigned n=0;n<waiters.size();++n)waiters[n]=std::jthread([&,n]{try{results[n]=request->wait_for(std::chrono::seconds(1));}catch(...){errors[n]=std::current_exception();}});
    gate->release();for(auto& thread:waiters)thread.join();for(unsigned n=0;n<waiters.size();++n)CHECK(!errors[n] && results[n] && results[n]==results[0] && results[n]->width==32 && results[n]->height==16);++cases;
    CHECK(request->get()==results[0] && request->wait_for(std::chrono::milliseconds(0))==results[0]);++cases;
    auto record=store->describe(results[0]);CHECK(record && record->member=="a.tga");++cases;
    request.reset();request=store->request("A.TGA");CHECK(request->wait_for(std::chrono::milliseconds(0))==results[0]);++cases;
    store.reset();CHECK(request->get()==results[0] && request->wait_for(std::chrono::milliseconds(0))==results[0]);++cases;
    rejects([&]{request->wait_for(std::chrono::milliseconds(-1));});rejects([&]{request->wait_for(std::chrono::milliseconds(60001));});guards+=2;++cases;
    {i::ImageStore damaged(r::Archive::open(bank({{"bad.tga",Bytes{1,2,3}}})));auto broken=damaged.request("bad.tga");rejects([&]{broken->wait_for(std::chrono::seconds(1));});rejects([&]{broken->get();});guards+=2;++cases;}
    {i::ImageStore failed(r::Archive::open(bank({{"a.tga",raster(1,1)}})),[]{throw std::runtime_error("injected image worker initialization failure");});auto broken=failed.request("a.tga");rejects([&]{broken->wait_for(std::chrono::seconds(1));});++guards;++cases;}
    {
        auto blocked=std::make_shared<Gate>(bank({{"a.tga",raster(1,1)},{"b.tga",raster(1,1)}}));auto loader=std::make_unique<i::ImageStore>(r::Archive::open(blocked));auto active=loader->request("a.tga");blocked->wait();auto pending=loader->request("b.tga");
        std::jthread close([owned=std::move(loader)]()mutable{owned.reset();});rejects([&]{pending->wait_for(std::chrono::seconds(1));});rejects([&]{pending->get();});guards+=2;++cases;
        blocked->release();close.join();CHECK(active->wait_for(std::chrono::milliseconds(0)) && active->get()->width==1);++cases;
    }
    std::ofstream out(argv[1]);out<<"{\"schema\":\"wa2.native-image-wait.v1\",\"cases\":"<<cases<<",\"guards\":"<<guards<<",\"completion_notification\":true,\"multiple_waiters_same_owner\":true,\"pending_shutdown_notifies\":true,\"synthetic_resources_only\":true,\"original_resources_opened\":false,\"devices_opened\":false,\"game_launched\":false,\"screen_access\":false,\"passed\":true}\n";CHECK(out);std::cout<<"PASS native image completion/timeout notification, shared owners and loader shutdown.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
