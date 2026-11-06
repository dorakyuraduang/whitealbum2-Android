#pragma once
#include "progress.hpp"
#include "script_runtime.hpp"

namespace wa2::script {
struct ChoiceItem {
    std::string text;
    std::uint32_t flag{};
    std::int32_t expected{};
    bool show_disabled{},enabled{},visible{};
};
// Selection indices are original insertion ordinals, including hidden rows.
class SceneChoices {
    std::shared_ptr<progress::Store> progress_;
    std::function<std::string_view()> scene_;
    std::vector<ChoiceItem> items_;
    std::optional<std::int32_t> result_;
    std::uint32_t history_flag_{992};
    std::uint64_t generation_{};
    bool active_{};
    void open(std::int32_t group);
public:
    SceneChoices(std::shared_ptr<progress::Store> progress,std::function<std::string_view()> scene);
    std::optional<CommandResult> dispatch(Command& command);
    std::span<const ChoiceItem> items()const noexcept{return items_;}
    bool active()const noexcept{return active_;}
    std::uint64_t generation()const noexcept{return generation_;}
    void choose(std::uint32_t ordinal);
    bool previously_chosen(std::uint32_t ordinal)const;
    void clear()noexcept{items_.clear();result_.reset();active_=false;++generation_;}
    std::vector<std::uint8_t> checkpoint()const;
    void restore(std::span<const std::uint8_t> bytes);
};
}
