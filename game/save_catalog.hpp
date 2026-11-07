#pragma once
#include "save_document.hpp"
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace wa2::game {
struct SavePreviewResult {std::optional<SavePreview> value;std::string diagnostic;};
// One bounded private-file read per request. A view owns at most one live
// request; destruction joins it before releasing its SaveSlots owner.
class SavePreviewRequest {
    mutable std::mutex mutex_;
    std::optional<SavePreviewResult> result_;
    std::thread worker_;
public:
    using Read=std::function<std::optional<SavePreview>()>;
    explicit SavePreviewRequest(Read,std::function<void()> initialize_worker={});
    ~SavePreviewRequest();
    SavePreviewRequest(const SavePreviewRequest&)=delete;
    SavePreviewRequest& operator=(const SavePreviewRequest&)=delete;
    std::optional<SavePreviewResult> get()const;
};
}
