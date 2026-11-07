#pragma once
#include "progress.hpp"
#include "script_runtime.hpp"

namespace wa2::script {
class SceneProgress {
    std::shared_ptr<progress::Store> store_;
public:
    explicit SceneProgress(std::shared_ptr<progress::Store> store):store_(std::move(store)){}
    std::optional<CommandResult> dispatch(Command& command);
    void event_image(unsigned bank,std::int32_t index);
    void image_file(std::string_view name);
};
}
