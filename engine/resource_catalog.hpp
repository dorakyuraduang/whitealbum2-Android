#pragma once
#include "image_store.hpp"
#include "image_reference.hpp"
#include <map>

namespace wa2::resource {
// The platform returns an owned source, or null only when a PAK is absent.
// Permission, provider and I/O failures must remain errors, never "not found".
using BankOpener=std::function<std::shared_ptr<Source>(std::string_view)>;
class Catalog {
    struct Bank {std::optional<Archive> archive;std::unique_ptr<image::ImageStore> images;};
    BankOpener opener_;
    std::map<std::string,Bank,std::less<>> banks_;
    std::map<std::string,std::shared_ptr<Source>,std::less<>> movies_;
    std::map<std::pair<std::string,std::string>,std::weak_ptr<const image::ColourMap>> colours_;
    std::vector<std::weak_ptr<const image::ColourMap>> restored_colours_;
    std::vector<std::shared_ptr<const image::ColourMap>> retained_colours_;
    std::shared_ptr<const image::ColourMap> intern_colour(std::shared_ptr<const image::ColourMap>);
    std::shared_ptr<const image::ColourMap> colour_map(std::string_view);
    void retain_colour(const std::shared_ptr<const image::ColourMap>&);
    Bank& bank(std::string_view path);
    struct Found {Bank* bank;std::size_t member;};
    Found find(std::string_view group,std::string_view name,bool optional=false);
public:
    explicit Catalog(BankOpener opener);
    const Archive& required(std::string_view group);
    const Archive* optional(std::string_view group,bool introductory=false);
    bool contains(std::string_view group,std::string_view name);
    std::shared_ptr<image::ImageRequest> image(std::string_view group,std::string_view name,std::int32_t chroma=256,std::string_view colour_map={},std::string_view secondary_map={});
    std::shared_ptr<image::ImageRequest> image(const image::Reference&);
    image::Reference reference(const std::shared_ptr<image::ImageRequest>&)const;
    image::Reference reference(const std::shared_ptr<const image::Raster>&)const;
    std::shared_ptr<Source> stream(std::string_view group,std::string_view name);
    std::vector<std::uint8_t> data(std::string_view group,std::string_view name,std::size_t maximum=128u*1024*1024);
    // The returned reader borrows this catalogue; the caller must not outlive it.
    MemberLoader members(std::string_view group);
    std::shared_ptr<Source> movie(std::string_view filename);
    std::size_t open_banks()const noexcept;
};
}
