#pragma once
#include "read_ahead_files.hpp"
#include <mutex>
#include <utility>

namespace wa2::platform::compat {
// Descriptor ownership only. No bytes are copied or retained by read ahead.
class ReadAheadStreams {
    std::mutex mutex_;
    std::vector<std::FILE*> streams_;
public:
    explicit ReadAheadStreams(std::size_t count):streams_(count){}
    ~ReadAheadStreams(){for(auto* stream:streams_)if(stream)std::fclose(stream);}
    void put(std::size_t index,std::FILE* stream){streams_.at(index)=stream;}
    std::FILE* take(std::size_t index){std::lock_guard lock(mutex_);return std::exchange(streams_.at(index),nullptr);}
    void bind(wr::ReadOnlyFileInfo& info,std::size_t index,const std::shared_ptr<ReadAheadStreams>& owner){
        info.open_read=[owner,index,fallback=info.open_read](std::uint32_t& error)->std::FILE*{
            if(auto* stream=owner->take(index)){error=0;return stream;}
            return fallback(error);
        };
    }
};
}
