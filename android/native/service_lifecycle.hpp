#pragma once
#include <span>

namespace wa2::android {
// A failed service may already own JNI references. Release that partially
// initialized service too, then unwind earlier owners in reverse order.
template<class Environment>struct JniService {
    bool (*initialize)(Environment*) noexcept;
    void (*release)(Environment*) noexcept;
};
template<class Environment>bool initialize_services(Environment* environment,std::span<const JniService<Environment>> services)noexcept{
    if(!environment || services.size()>8)return false;
    for(const auto& service:services)if(!service.initialize || !service.release)return false;
    for(std::size_t n=0;n<services.size();++n)if(!services[n].initialize(environment)){
        for(auto release=n+1;release>0;--release)services[release-1].release(environment);return false;
    }
    return true;
}
template<class Environment>void release_services(Environment* environment,std::span<const JniService<Environment>> services)noexcept{
    if(!environment)return;for(auto n=services.size();n>0;--n)if(services[n-1].release)services[n-1].release(environment);
}
}
