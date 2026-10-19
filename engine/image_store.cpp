#include "image_store.hpp"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace wa2::image {
struct ImageRequest::State {
    mutable std::mutex mutex;
    std::shared_ptr<const Raster> image;
    std::string error;
    std::size_t member{};
};
ImageRequest::ImageRequest(std::shared_ptr<State> state):state_(std::move(state)){}
std::shared_ptr<const Raster> ImageRequest::get()const{
    std::lock_guard lock(state_->mutex);if(!state_->error.empty())throw Error(state_->error);return state_->image;
}
struct ImageStore::Impl {
    resource::Archive archive;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::shared_ptr<ImageRequest::State>> pending;
    std::map<std::size_t,std::weak_ptr<ImageRequest>> cache;
    std::map<std::size_t,std::weak_ptr<const Raster>> decoded;
    bool stop{};
    std::thread worker;
    Impl(resource::Archive value,std::function<void()> initialize):archive(std::move(value)){
        worker=std::thread([this,initialize=std::move(initialize)]{
            std::string initialization_error;
            try{if(initialize)initialize();}catch(const std::exception& error){initialization_error=error.what();}
            catch(...){initialization_error="native image worker initialization failed";}
            for(;;){
                std::shared_ptr<ImageRequest::State> request;
                {std::unique_lock lock(mutex);wake.wait(lock,[&]{return stop || !pending.empty();});if(stop)break;
                    request=std::move(pending.front());pending.pop_front();}
                // A scene can abandon a request while the previous one loads.
                if(request.use_count()==1)continue;
                std::shared_ptr<const Raster> result;std::string error;
                try{
                    if(!initialization_error.empty())throw Error(initialization_error);
                    auto source=archive.load(request->member);
                    result=std::make_shared<const Raster>(source.size()>=2 && source[0]=='B' && source[1]=='M'
                        ?decode_game_bmp(source):decode_game_tga(source));
                }catch(const std::exception& failure){error=failure.what();}
                catch(...){error="native image loading failed";}
                if(result){std::lock_guard lock(mutex);decoded[request->member]=result;}
                std::lock_guard lock(request->mutex);request->image=std::move(result);request->error=std::move(error);
            }
        });
    }
    ~Impl(){
        {std::lock_guard lock(mutex);stop=true;
            for(const auto& item:pending){std::lock_guard state_lock(item->mutex);item->error="native image loader closed";}
            pending.clear();}
        wake.notify_all();if(worker.joinable())worker.join();
    }
};
ImageStore::ImageStore(resource::Archive archive,std::function<void()> initialize)
    :impl_(std::make_unique<Impl>(std::move(archive),std::move(initialize))){}
ImageStore::~ImageStore()=default;
std::shared_ptr<ImageRequest> ImageStore::request(std::string_view name){
    auto& self=*impl_;const auto index=self.archive.find(name);
    if(index==resource::Archive::missing)throw Error("missing image resource "+std::string(name));
    std::lock_guard lock(self.mutex);
    if(const auto found=self.cache.find(index);found!=self.cache.end())if(auto request=found->second.lock())return request;
    auto state=std::make_shared<ImageRequest::State>();state->member=index;
    if(const auto found=self.decoded.find(index);found!=self.decoded.end())state->image=found->second.lock();
    if(!state->image && self.pending.size()>=8)throw Error("native image loading queue is full");
    auto request=std::shared_ptr<ImageRequest>(new ImageRequest(state));
    std::erase_if(self.cache,[](const auto& item){return item.second.expired();});
    std::erase_if(self.decoded,[](const auto& item){return item.second.expired();});
    self.cache[index]=request;
    if(!state->image){self.pending.push_back(std::move(state));self.wake.notify_one();}
    return request;
}
}
