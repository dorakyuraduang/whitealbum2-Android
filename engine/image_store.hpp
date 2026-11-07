#pragma once
#include "raster_image.hpp"
#include "image_colours.hpp"
#include "resource_archive.hpp"
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>

namespace wa2::image {
// Per archive, separate from live pictures owned by scenes. Retaining a small
// recent set avoids reading/decompressing a picture again after a scene drops
// its final request. No rendered frame or mutable game state enters the cache.
struct ImageCacheLimits {
    std::size_t bytes{8u*1024*1024},entries{8},image_bytes{4u*1024*1024};
};
struct ImageCacheStatistics {
    std::size_t retained_bytes{},retained_entries{};
    std::uint64_t hits{},decode_attempts{},evictions{};
};
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
    explicit ImageStore(resource::Archive archive,std::function<void()> initialize_worker={},ImageCacheLimits={});
    ~ImageStore();
    ImageStore(const ImageStore&)=delete;
    ImageStore& operator=(const ImageStore&)=delete;
    std::shared_ptr<ImageRequest> request(std::string_view name,ImageStyle style={});
    std::optional<Record> describe(const std::shared_ptr<ImageRequest>&)const;
    std::optional<Record> describe(const std::shared_ptr<const Raster>&)const;
    ImageCacheStatistics cache_statistics()const;
    // Drop cache-owned pixels only. Live requests/rasters remain valid, and
    // subsequent requests or pending completions may warm the cache again.
    void clear_cache();
};
}
