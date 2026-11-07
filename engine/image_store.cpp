#include "image_store.hpp"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <tuple>

namespace wa2::image {
struct ImageRequest::State {
    mutable std::mutex mutex;
    std::condition_variable ready;
    std::shared_ptr<const Raster> image;
    std::string error;
    std::size_t member{};
    ImageStyle style;
    std::shared_ptr<const void> owner;
};
ImageRequest::ImageRequest(std::shared_ptr<State> state):state_(std::move(state)){}
std::shared_ptr<const Raster> ImageRequest::get()const{
    std::lock_guard lock(state_->mutex);if(!state_->error.empty())throw Error(state_->error);return state_->image;
}
std::shared_ptr<const Raster> ImageRequest::wait_for(std::chrono::milliseconds timeout)const{
    if(timeout.count()<0 || timeout.count()>60000)throw std::invalid_argument("Native image wait timeout is out of range");
    std::unique_lock lock(state_->mutex);state_->ready.wait_for(lock,timeout,[&]{return bool(state_->image) || !state_->error.empty();});
    if(!state_->error.empty())throw Error(state_->error);return state_->image;
}
struct ImageStore::Impl {
    resource::Archive archive;
    std::shared_ptr<const void> identity{std::make_shared<int>(0)};
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::shared_ptr<ImageRequest::State>> pending;
    using Key=std::tuple<std::size_t,std::int32_t,std::shared_ptr<const ColourMap>>;
    std::map<Key,std::weak_ptr<ImageRequest>> cache;
    std::map<Key,std::weak_ptr<const Raster>> decoded;
    struct Retained {Key key;std::shared_ptr<const Raster> image;};
    std::vector<Retained> retained;
    ImageCacheLimits limits;
    std::size_t retained_bytes{};
    std::uint64_t hits{},decode_attempts{},evictions{};
    bool stop{};
    std::thread worker;
    bool touch(const Key& key){
        const auto found=std::find_if(retained.begin(),retained.end(),[&](const auto& item){return item.key==key;});
        if(found==retained.end())return false;std::rotate(found,found+1,retained.end());return true;
    }
    void retain(const Key& key,const std::shared_ptr<const Raster>& image){
        const auto size=image->rgba.size();if(!limits.entries || size>limits.image_bytes || size>limits.bytes)return;
        if(touch(key))return;
        while(!retained.empty() && (retained.size()>=limits.entries || retained_bytes>limits.bytes-size)){
            retained_bytes-=retained.front().image->rgba.size();retained.erase(retained.begin());++evictions;
        }
        retained.push_back({key,image});retained_bytes+=size;
    }
    Impl(resource::Archive value,std::function<void()> initialize,ImageCacheLimits policy):archive(std::move(value)),limits(policy){
        retained.reserve(limits.entries);
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
                {std::lock_guard lock(mutex);++decode_attempts;}
                std::shared_ptr<const Raster> result;std::string error;
                try{
                    if(!initialization_error.empty())throw Error(initialization_error);
                    auto source=archive.load(request->member);
                    auto pixels=source.size()>=2 && source[0]=='B' && source[1]=='M'?decode_game_bmp(source):decode_game_tga(source);
                    apply_image_style(pixels,request->style);result=std::make_shared<const Raster>(std::move(pixels));
                }catch(const std::exception& failure){error=failure.what();}
                catch(...){error="native image loading failed";}
                if(result){std::lock_guard lock(mutex);const Key key{request->member,request->style.chroma,request->style.colours};decoded[key]=result;
                    // An abandoned in-flight load must not displace a useful
                    // recent image. Its private result can still be released.
                    if(request.use_count()>1)retain(key,result);}
                {std::lock_guard lock(request->mutex);request->image=std::move(result);request->error=std::move(error);}
                request->ready.notify_all();
            }
        });
    }
    ~Impl(){
        {std::lock_guard lock(mutex);stop=true;
            for(const auto& item:pending){{std::lock_guard state_lock(item->mutex);item->error="native image loader closed";}item->ready.notify_all();}
            pending.clear();}
        wake.notify_all();if(worker.joinable())worker.join();
    }
};
ImageStore::ImageStore(resource::Archive archive,std::function<void()> initialize,ImageCacheLimits limits){
    const bool disabled=!limits.bytes && !limits.entries && !limits.image_bytes;
    if(!disabled && (!limits.bytes || !limits.entries || !limits.image_bytes || limits.bytes>32u*1024*1024 || limits.entries>64 || limits.image_bytes>limits.bytes))
        throw std::invalid_argument("Native image retention limits are out of range");
    impl_=std::make_unique<Impl>(std::move(archive),std::move(initialize),limits);
}
ImageStore::~ImageStore()=default;
std::shared_ptr<ImageRequest> ImageStore::request(std::string_view name,ImageStyle style){
    validate_image_style(style);
    auto& self=*impl_;const auto index=self.archive.find(name);
    if(index==resource::Archive::missing)throw Error("missing image resource "+std::string(name));
    std::lock_guard lock(self.mutex);
    const Impl::Key key{index,style.chroma,style.colours};
    if(const auto found=self.cache.find(key);found!=self.cache.end())if(auto request=found->second.lock()){if(self.touch(key))++self.hits;return request;}
    auto state=std::make_shared<ImageRequest::State>();state->member=index;state->style=std::move(style);state->owner=self.identity;
    if(const auto found=self.decoded.find(key);found!=self.decoded.end())state->image=found->second.lock();
    if(state->image){self.retain(key,state->image);++self.hits;}
    if(!state->image && self.pending.size()>=8)throw Error("native image loading queue is full");
    auto request=std::shared_ptr<ImageRequest>(new ImageRequest(state));
    std::erase_if(self.cache,[](const auto& item){return item.second.expired();});
    std::erase_if(self.decoded,[](const auto& item){return item.second.expired();});
    self.cache[key]=request;
    if(!state->image){self.pending.push_back(std::move(state));self.wake.notify_one();}
    return request;
}
std::optional<ImageStore::Record> ImageStore::describe(const std::shared_ptr<ImageRequest>& request)const{
    if(!request || request->state_->owner!=impl_->identity)return {};
    const auto& state=*request->state_;return Record{impl_->archive.members()[state.member].name,state.style};
}
std::optional<ImageStore::Record> ImageStore::describe(const std::shared_ptr<const Raster>& raster)const{
    if(!raster)return {};auto& self=*impl_;std::lock_guard lock(self.mutex);
    for(const auto& [key,value]:self.decoded)if(value.lock()==raster)return Record{self.archive.members()[std::get<0>(key)].name,{std::get<1>(key),std::get<2>(key)}};
    return {};
}
ImageCacheStatistics ImageStore::cache_statistics()const{
    auto& self=*impl_;std::lock_guard lock(self.mutex);return {self.retained_bytes,self.retained.size(),self.hits,self.decode_attempts,self.evictions};
}
void ImageStore::clear_cache(){
    auto& self=*impl_;std::lock_guard lock(self.mutex);self.retained.clear();self.retained_bytes=0;
    std::erase_if(self.decoded,[](const auto& item){return item.second.expired();});
    std::erase_if(self.cache,[](const auto& item){return item.second.expired();});
}
}
