#pragma once
#include "raster_image.hpp"
#include "resource_archive.hpp"
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
};
class ImageStore {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    explicit ImageStore(resource::Archive archive,std::function<void()> initialize_worker={});
    ~ImageStore();
    ImageStore(const ImageStore&)=delete;
    ImageStore& operator=(const ImageStore&)=delete;
    std::shared_ptr<ImageRequest> request(std::string_view name);
};
}
