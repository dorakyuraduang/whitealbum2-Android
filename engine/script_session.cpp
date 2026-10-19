#include "script_session.hpp"

namespace wa2::script {
Session::Session(Library& library):Session(library.functions(),library.globals(),[&library](std::string_view scene){return library.load(scene);}){}
Session::Session(const Functions& functions,Globals& globals,Loader loader)
    :functions_(functions),globals_(globals),loader_(std::move(loader)){
    if(!loader_)throw Error("missing native scene loader");
}
Session::Scene Session::prepare(std::string_view name,std::int32_t label,std::int32_t caller){
    auto program=loader_(name);
    auto runtime=std::make_unique<Runtime>(program,functions_,globals_);
    if(!runtime->start(label,caller))throw Error("missing native scene entry "+std::string(name)+":"+std::to_string(label));
    return {std::move(program),std::move(runtime)};
}
void Session::start(std::string_view scene,std::int32_t label,std::int32_t caller){
    if(advancing_)throw Error("native scene replaced during command callback");
    auto next=prepare(scene,label,caller);
    scenes_.clear();scenes_.push_back(std::move(next));replacement_.reset();child_.reset();
}
CommandResult Session::dispatch(std::size_t index,Command& command,const Host& host){
    auto count=[&](std::size_t value){if(command.arguments.size()!=value)throw Error("invalid native script intrinsic parameters");};
    switch(command.index){
    case 0:count(2);replacement_=Request{command.arguments[0].text(),command.arguments[1].integer()};return {true,true,{}};
    case 1:count(2);child_=Request{command.arguments[0].text(),command.arguments[1].integer()};return {true,true,{}};
    case 2:count(1);child_=Request{scenes_[index].program->name(),command.arguments[0].integer()};return {true,true,{}};
    case 3:count(0);return {true,true,{}}; // run yields this frame; it does not stop the scene.
    case 5:count(0);return {true,false,{}}; // ret's callback is empty; flow opcode 1 stops execution.
    case 6:count(1);return {true,false,command.arguments[0].type()==Type::text?Value{}:Value(command.arguments[0].integer())};
    case 7:count(1);return {true,false,command.arguments[0].type()==Type::text?Value(0.0f):Value(command.arguments[0].real())};
    default:
        if(!host)throw Error("unimplemented native script command "+functions_.at(command.index).name);
        return host(command);
    }
}
RunState Session::advance_scene(std::size_t index,const Host& host,std::uint32_t& remaining){
    if(index+1<scenes_.size()){
        const auto result=advance_scene(index+1,host,remaining);
        if(result==RunState::stopped)scenes_.resize(index+1);
        // The original caller resumes on the following update, after its
        // completed child is destroyed; it does not execute another frame here.
        return result==RunState::slice_exhausted?result:RunState::yielded;
    }
    auto& runtime=*scenes_[index].runtime;
    if(!runtime.running())return RunState::stopped;
    if(!remaining)return RunState::slice_exhausted;
    const auto before=runtime.operations();
    const auto state=runtime.advance([&](Command& command){return dispatch(index,command,host);},remaining);
    const auto consumed=runtime.operations()-before;
    if(consumed>remaining)throw Error("native script exceeded its operation slice");
    remaining-=std::uint32_t(consumed);
    if(replacement_)return RunState::yielded;
    if(child_){
        if(scenes_.size()>=64)throw Error("native scene call depth exceeded");
        auto next=prepare(child_->name,child_->label);child_.reset();scenes_.push_back(std::move(next));
        const auto result=advance_scene(index+1,host,remaining);
        if(result==RunState::stopped)scenes_.resize(index+1);
        return result==RunState::slice_exhausted?result:RunState::yielded;
    }
    return state;
}
RunState Session::advance(const Host& host,std::uint32_t maximum_operations){
    if(advancing_)throw Error("recursive native scene update");
    if(!maximum_operations)throw Error("zero native scene operation slice");
    if(scenes_.empty())return RunState::stopped;
    struct Scope {bool& flag;~Scope(){flag=false;}} scope{advancing_};advancing_=true;
    const auto state=advance_scene(0,host,maximum_operations);
    if(replacement_){
        auto next=prepare(replacement_->name,replacement_->label);
        scenes_.clear();scenes_.push_back(std::move(next));replacement_.reset();child_.reset();
        return RunState::yielded;
    }
    return state;
}
}
