#pragma once
#include "progress.hpp"
#include <string>
#include <string_view>

namespace wa2::script {
// Native reading state, indexed by the original persisted script-name list.
class SceneReadHistory {
    std::shared_ptr<progress::Store> progress_;
    std::string scene_;
    std::uint32_t index_{};
    std::int32_t last_line_{},override_{};
public:
    explicit SceneReadHistory(std::shared_ptr<progress::Store> progress):progress_(std::move(progress)){}
    void enter(std::string_view scene);
    void seen(std::string_view scene,std::int32_t line);
    void finish();
    std::int32_t status(bool replay=false)const;
    void force(std::int32_t value){override_=value<0?0:value;}
    std::int32_t last_line()const noexcept{return last_line_;}
    std::uint32_t script_index()const noexcept{return index_;}
    std::vector<std::uint8_t> checkpoint()const;
    void restore(std::span<const std::uint8_t> bytes);
};
}
