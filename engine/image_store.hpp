#pragma once
#include "raster_image.hpp"
#include "image_colours.hpp"
#include "resource_archive.hpp"
#include <chrono>
#include <functional>
#include <memory>

namespace wa2::image {
class ImageRequest {
    struct State;
    std::shared_ptr<State> state_;
    explicit ImageRequest(std::shared_ptr<State>);
    friend class ImageStore;
public:
    // Null while loading, a complete immutable image on success, or an error.
    // A partially decoded image is never exposed to rendering.
    std::shared_ptr<const Raster> get()const;
    // Resource restoration can wait for publication without polling. A
    // timeout returns null; decoder/worker/shutdown errors remain errors.
    std::shared_ptr<const Raster> wait_for(std::chrono::milliseconds timeout)const;
};
class ImageStore {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    struct Record {std::string member;ImageStyle style;};
    explicit ImageStore(resource::Archive archive,std::function<void()> initialize_worker={});
    ~ImageStore();
    ImageStore(const ImageStore&)=delete;
    ImageStore& operator=(const ImageStore&)=delete;
    std::shared_ptr<ImageRequest> request(std::string_view name,ImageStyle style={});
    std::optional<Record> describe(const std::shared_ptr<ImageRequest>&)const;
    std::optional<Record> describe(const std::shared_ptr<const Raster>&)const;
};
}
