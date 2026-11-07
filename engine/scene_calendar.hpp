#pragma once
#include "script_runtime.hpp"
#include "image_store.hpp"
#include "scene_sprite.hpp"
#include "image_reference.hpp"

namespace wa2::script {
struct CalendarDate {
    std::int32_t year{},month{},day{},weekday{};
    bool holiday{};
    bool operator==(const CalendarDate&)const=default;
};
class SceneCalendar {
public:
    using Loader=std::function<std::shared_ptr<image::ImageRequest>(std::string_view)>;
    explicit SceneCalendar(Loader,std::function<std::int32_t()> theme={});
    std::optional<CommandResult> dispatch(Command&);
    void update(const std::function<std::int32_t(std::int32_t)>& duration={});
    bool busy()const noexcept{return active_;}
    const CalendarDate& date()const noexcept{return date_;}
    std::span<const image::Sprite> sprites();
    std::vector<std::uint8_t> checkpoint()const;
    void restore(std::span<const std::uint8_t>);
    void prepare_restored_assets(resource::Catalog&,const image::Resolve&);
    static CalendarDate date(std::int32_t year,std::int32_t month,std::int32_t day,std::int32_t weekday=-1);
    static bool supports_display(const CalendarDate&)noexcept;
private:
    Loader loader_;
    std::function<std::int32_t()> theme_;
    std::array<std::shared_ptr<image::ImageRequest>,3> requests_;
    std::array<std::shared_ptr<const image::Raster>,3> images_;
    std::shared_ptr<const image::Raster> line_;
    std::vector<image::Sprite> sprites_;
    CalendarDate date_;
    std::uint32_t phase_{},elapsed_{},age_{},bar_elapsed_{};
    std::uint8_t opacity_{},digit_opacity_{255};
    float bar_width_{},bar_height_{},bar_offset_{};
    bool active_{},digits_{},background_{},bars_{},alternate_{};
    std::array<std::shared_ptr<image::ImageRequest>,3> load(bool alternate)const;
    bool ready();
};
}
