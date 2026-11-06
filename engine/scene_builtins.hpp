#pragma once
#include "native_random.hpp"
#include "script_runtime.hpp"

namespace wa2::script {
class SceneBuiltins {
    wa2::Random& random_;
    std::function<void(std::string_view)> diagnostic_;
public:
    explicit SceneBuiltins(wa2::Random& random,std::function<void(std::string_view)> diagnostic={}):random_(random),diagnostic_(std::move(diagnostic)){}
    std::optional<CommandResult> dispatch(Command& command);
};
}
