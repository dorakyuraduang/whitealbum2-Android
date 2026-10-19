#pragma once
#include "image_store.hpp"
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
    Bank& bank(std::string_view path);
    struct Found {Bank* bank;std::size_t member;};
    Found find(std::string_view group,std::string_view name);
public:
    explicit Catalog(BankOpener opener);
    const Archive& required(std::string_view group);
    const Archive* optional(std::string_view group,bool introductory=false);
    std::shared_ptr<image::ImageRequest> image(std::string_view group,std::string_view name);
    std::shared_ptr<Source> stream(std::string_view group,std::string_view name);
    std::vector<std::uint8_t> data(std::string_view group,std::string_view name);
    std::shared_ptr<Source> movie(std::string_view filename);
    std::size_t open_banks()const noexcept;
};
}
