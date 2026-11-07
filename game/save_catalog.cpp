#include "save_catalog.hpp"
#include <stdexcept>

namespace wa2::game {
SavePreviewRequest::SavePreviewRequest(Read read,std::function<void()> initialize){
    if(!read)throw std::invalid_argument("Missing native save preview reader");
    worker_=std::thread([this,read=std::move(read),initialize=std::move(initialize)]{
        SavePreviewResult result;
        try{if(initialize)initialize();result.value=read();}
        catch(const std::exception& error){result.diagnostic=error.what();}
        catch(...){result.diagnostic="Unknown native save preview read failure";}
        std::lock_guard lock(mutex_);result_=std::move(result);
    });
}
SavePreviewRequest::~SavePreviewRequest(){if(worker_.joinable())worker_.join();}
std::optional<SavePreviewResult> SavePreviewRequest::get()const{std::lock_guard lock(mutex_);return result_;}
}
