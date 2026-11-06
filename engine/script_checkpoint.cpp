#include "script_runtime.hpp"
#include "script_session.hpp"
#include "checkpoint_codec.hpp"
#include <algorithm>
#include <bit>
#include <limits>

namespace wa2::script {
namespace {
void value(checkpoint::Writer& out,const Value& v){
    out.u32(std::uint32_t(v.type()));out.boolean(v.uses_default_text());
    if(v.type()==Type::text)out.string(v.text());else if(v.type()==Type::real)out.u32(std::bit_cast<std::uint32_t>(v.real()));else out.i32(v.integer());
}
Value value(checkpoint::Reader& in){
    const auto type=in.u32();const bool default_text=in.boolean();
    if(default_text && type!=5)throw Error("Invalid default-text checkpoint value");
    if(type==5){auto text=in.string();if(default_text){if(!text.empty())throw Error("Invalid default-text payload");return Value::default_text();}return Value(std::move(text));}
    if(type==4)return Value(std::bit_cast<float>(in.u32()));
    if(type==3)return Value(in.i32());throw Error("Invalid native checkpoint value type");
}
}
std::vector<std::uint8_t> Globals::checkpoint()const{
    checkpoint::Writer out;out.u32(std::uint32_t(variables_.size()));
    for(const auto& variable:variables_){out.string(variable.name);out.u32(std::uint32_t(variable.storage));value(out,variable.value);}
    return out.seal("LSGL");
}
void Globals::restore(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("LSGL",bytes);if(in.u32()!=variables_.size())throw Error("Native global variable catalogue changed");
    auto next=variables_;
    for(auto& variable:next){
        if(in.string(4096)!=variable.name || in.u32()!=std::uint32_t(variable.storage))throw Error("Native global variable schema changed");
        auto saved=value(in);
        if(variable.storage==Type::text){if(saved.type()!=Type::text || saved.uses_default_text())throw Error("Invalid saved global text");}
        else if(variable.storage==Type::real){if(saved.type()!=Type::real)throw Error("Invalid saved global real");}
        else{
            if(saved.type()!=Type::integer)throw Error("Invalid saved global integer");const auto n=saved.integer();
            if((variable.storage==Type::byte && (n<0 || n>255)) || (variable.storage==Type::short_integer && (n<-32768 || n>32767)))throw Error("Saved global integer exceeds storage range");
        }
        variable.value=std::move(saved);
    }
    in.finish();variables_=std::move(next);
}
std::vector<std::uint8_t> Runtime::checkpoint()const{
    if(advancing_)throw Error("Cannot checkpoint a script during a command callback");
    checkpoint::Writer out;out.string(program_->name());out.u32(program_->version());out.u64(program_->fingerprint());
    out.u32(offset_);out.i32(caller_);out.boolean(running_);out.u64(operations_);out.u32(std::uint32_t(depth_));
    for(std::size_t n=0;n<=depth_;++n){const auto& s=stack_[n];out.u32(s.kind);out.i32(s.local_index);out.u32(s.reference);value(out,s.value);}
    for(auto v:integers_)out.i32(v);for(auto v:reals_)out.u32(std::bit_cast<std::uint32_t>(v));
    out.u32(std::uint32_t(flow_depth_));
    for(std::size_t n=0;n<=flow_depth_;++n){const auto& f=flows_[n];out.u32(f.kind);out.u32(f.end);out.u32(f.alternate);out.u32(f.again);out.u32(f.begin);out.i32(f.condition);out.u32(std::uint32_t(f.cases.size()));for(auto [target,test]:f.cases){out.u32(target);out.i32(test);}}
    out.boolean(pending_.has_value());if(pending_){out.u32(pending_->index);out.u32(pending_->continuation);out.u32(std::uint32_t(pending_->arguments.size()));for(const auto& v:pending_->arguments)value(out,v);}
    return out.seal("LSRT");
}
void Runtime::restore(std::span<const std::uint8_t> bytes){
    if(advancing_)throw Error("Cannot restore a script during a command callback");
    auto in=checkpoint::Reader::open("LSRT",bytes);
    if(in.string(240)!=program_->name() || in.u32()!=program_->version() || in.u64()!=program_->fingerprint())throw Error("Saved script content does not match the resource");
    Runtime next(program_,functions_,globals_);next.offset_=in.u32();next.caller_=in.i32();next.running_=in.boolean();next.operations_=in.u64();
    if(!program_->instruction_offset(next.offset_,!next.running_) && next.offset_!=0)throw Error("Saved script position is not an instruction boundary");
    if(next.running_ && !program_->instruction_offset(next.offset_))throw Error("Invalid running script position");
    next.depth_=in.u32();if(next.depth_>=next.stack_.size())throw Error("Saved script value stack exceeds limit");
    for(std::size_t n=0;n<=next.depth_;++n){auto& s=next.stack_[n];s.kind=in.u32();s.local_index=in.i32();s.reference=in.u32();s.value=value(in);
        switch(s.kind){
        case 0:if(n)throw Error("Invalid active script stack slot");break;
        case 1:if(s.reference>=globals_.variables().size())throw Error("Saved global reference out of range");break;
        case 2:if(s.reference>=52 || s.local_index>=16)throw Error("Saved local reference out of range");break;
        case 3:if(s.reference>=program_->strings().size())throw Error("Saved script text reference out of range");break;
        case 5:break;default:throw Error("Unknown saved script stack kind");
        }
    }
    for(auto& v:next.integers_)v=in.i32();for(auto& v:next.reals_)v=std::bit_cast<float>(in.u32());
    next.flow_depth_=in.u32();if(next.flow_depth_>=next.flows_.size())throw Error("Saved script flow stack exceeds limit");
    for(std::size_t n=0;n<=next.flow_depth_;++n){auto& f=next.flows_[n];f.kind=in.u32();f.end=in.u32();f.alternate=in.u32();f.again=in.u32();f.begin=in.u32();f.condition=in.i32();const auto count=in.u32();
        if((n==0 && f.kind!=0) || (n && f.kind!=2 && f.kind!=3 && f.kind!=5 && f.kind!=6 && f.kind!=7) || count>64)throw Error("Invalid saved script flow kind/count");
        for(auto target:{f.end,f.alternate,f.again,f.begin})if(target && !program_->instruction_offset(target,true))throw Error("Invalid saved script branch position");
        if(count && f.kind!=7)throw Error("Unexpected saved switch cases");
        for(unsigned i=0;i<count;++i){const auto target=in.u32();const auto test=in.i32();if(!program_->instruction_offset(target))throw Error("Invalid saved switch target");f.cases.emplace_back(target,test);}
    }
    if(in.boolean()){
        if(!next.running_)throw Error("Stopped saved script has a pending command");
        Pending p;p.index=in.u32();p.continuation=in.u32();const auto count=in.u32();const auto& function=functions_.at(p.index);
        if(count!=function.parameters.size() || count>16)throw Error("Saved command signature changed");
        for(unsigned n=0;n<count;++n){auto v=value(in);const auto type=function.parameters[n];
            if((type==5 && (v.type()!=Type::text || v.uses_default_text())) || (type==4 && v.type()!=Type::real) || ((type<=3 || type==7) && v.type()!=Type::integer))throw Error("Saved command argument type changed");
            p.arguments.push_back(std::move(v));
        }
        next.pending_=std::move(p);
    }
    in.finish();stack_=std::move(next.stack_);integers_=next.integers_;reals_=next.reals_;flows_=std::move(next.flows_);
    depth_=next.depth_;flow_depth_=next.flow_depth_;pending_=std::move(next.pending_);offset_=next.offset_;caller_=next.caller_;running_=next.running_;operations_=next.operations_;
}
std::vector<std::uint8_t> Session::checkpoint()const{
    if(advancing_ || replacement_ || child_)throw Error("Cannot checkpoint a scene call in progress");
    checkpoint::Writer out;out.blob(globals_.checkpoint());out.u32(std::uint32_t(scenes_.size()));
    for(const auto& scene:scenes_){out.string(scene.program->name());out.blob(scene.runtime->checkpoint());}
    return out.seal("LSSN");
}
void Session::restore(std::span<const std::uint8_t> bytes){
    if(advancing_)throw Error("Cannot restore during a scene command callback");
    auto in=checkpoint::Reader::open("LSSN",bytes);auto next_globals=globals_;next_globals.restore(in.blob());
    const auto count=in.u32();if(count>64)throw Error("Saved scene depth exceeds limit");std::vector<Scene> next;
    for(unsigned n=0;n<count;++n){auto name=in.string(240);
        if(name.empty() || name.find_first_of("/\\")!=std::string::npos || name.find('\0')!=std::string::npos)throw Error("Invalid saved scene name");
        auto program=loader_(name);if(!program || program->name()!=name)throw Error("Saved scene loader returned a different program");
        auto runtime=std::make_unique<Runtime>(program,functions_,globals_);runtime->restore(in.blob());next.push_back({std::move(program),std::move(runtime)});
    }
    in.finish();globals_=std::move(next_globals);scenes_=std::move(next);replacement_.reset();child_.reset();
}
}
