#pragma once
#include "raster_image.hpp"
#include "resource_archive.hpp"
#include "scene_sprite.hpp"
#include <array>
#include <memory>
#include <string_view>

namespace wa2::text {
struct Colour {std::uint8_t r{255},g{255},b{255};bool operator==(const Colour&)const=default;};
struct TextStyle {
    int size{28},columns{28},rows{4},spacing{},line_gap{14};
    int source_size{32},cell{40};
    Colour colour{},shadow{0,0,0};
    bool outline{true};
};
struct Letter {
    std::uint16_t code{};
    float x{},y{},scale{1};
    Colour colour{},shadow{0,0,0};
    std::uint32_t page{},time{},atlas_slot{};
    bool small{},outline{true};
};
struct TextPage {std::uint32_t duration{};float end_x{},end_y{};};
struct Layout {std::vector<Letter> letters;std::vector<TextPage> pages;TextStyle style;};
// Raw game encoding is deliberately preserved. These bytes index the original
// font atlases; interpreting them as platform Unicode would select other glyphs.
Layout layout(std::string_view raw,const TextStyle& style={});
std::string_view speaker_name(int index);
int speaker_style(int index,std::string_view name={});
Colour speaker_colour(int style);

class PreparedText {
    friend class FontBook;
    Layout layout_;
    std::shared_ptr<const image::Raster> image_;
public:
    const Layout& metrics()const noexcept{return layout_;}
    // Appends into a retained host buffer. This does not decode, lay out text,
    // allocate textures, or allocate a fresh vector on each logic/render tick.
    void append(std::vector<image::Sprite>& out,std::uint32_t page,std::uint32_t budget,
                float x,float y,std::uint8_t opacity=255)const;
    std::size_t image_bytes()const noexcept{return image_?image_->rgba.size():0;}
};
class FontBook {
    struct Atlas {std::vector<std::uint8_t> source;image::TgaLayout layout;};
    resource::Archive archive_;
    std::array<std::unique_ptr<Atlas>,4> atlases_;
    const Atlas& atlas(unsigned index);
public:
    explicit FontBook(resource::Archive archive):archive_(std::move(archive)){}
    PreparedText prepare(Layout layout);
    std::size_t source_bytes()const noexcept;
};
}
