#pragma once
#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

namespace wr {
// Track live native resources for ordered shutdown without retaining every
// control block ever created. Collection is amortized over resource creation.
template<class T> class WeakRegistry {
    std::vector<std::weak_ptr<T>> entries_;
    unsigned since_collection_{};
public:
    void add(const std::shared_ptr<T>& value) {
        if(++since_collection_==256){collect();since_collection_=0;}
        entries_.push_back(value);
    }
    void collect(){std::erase_if(entries_,[](const auto& value){return value.expired();});}
    std::size_t size() const{return entries_.size();}
    auto begin(){return entries_.begin();}
    auto end(){return entries_.end();}
};
}
