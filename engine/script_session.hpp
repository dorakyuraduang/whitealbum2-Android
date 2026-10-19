#pragma once
#include "script_runtime.hpp"

namespace wa2::script {
using Loader=std::function<std::shared_ptr<const Program>(std::string_view)>;
// Owns native scene instances. Global variables are shared; local variables,
// argument stacks and suspended commands belong to each invocation.
class Session {
public:
    explicit Session(Library& library);
    Session(const Functions& functions,Globals& globals,Loader loader);
    void start(std::string_view scene,std::int32_t label=0,std::int32_t caller=0);
    RunState advance(const Host& host,std::uint32_t maximum_operations=4096);
    std::size_t scene_depth() const noexcept {return scenes_.size();}
    const Runtime* active() const noexcept {return scenes_.empty()?nullptr:scenes_.back().runtime.get();}
private:
    struct Scene {std::shared_ptr<const Program> program;std::unique_ptr<Runtime> runtime;};
    struct Request {std::string name;std::int32_t label{};};
    const Functions& functions_;
    Globals& globals_;
    Loader loader_;
    std::vector<Scene> scenes_;
    std::optional<Request> replacement_,child_;
    bool advancing_{};
    Scene prepare(std::string_view name,std::int32_t label,std::int32_t caller=0);
    RunState advance_scene(std::size_t index,const Host& host,std::uint32_t& remaining);
    CommandResult dispatch(std::size_t index,Command& command,const Host& host);
};
}
