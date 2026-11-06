#include "scene_read_history.hpp"
#include "checkpoint_codec.hpp"
#include "script_runtime.hpp"
#include <array>
#include <algorithm>

namespace wa2::script {
namespace {
constexpr std::array<std::string_view,185> catalogue={
#include "script_read_catalogue.inc"
};
std::uint32_t row(std::string_view scene){const auto found=std::find(catalogue.begin(),catalogue.end(),scene);return found==catalogue.end()?0u:std::uint32_t(found-catalogue.begin());}
}
void SceneReadHistory::enter(std::string_view scene){
    if(!progress_)throw Error("Missing native reading-history storage");
    const auto next=row(scene);
    if(next!=index_){
        if(last_line_!=0 || index_==29 || index_==30 || index_==31){
            const auto marker=std::int64_t(last_line_)+1;
            if(marker>=0 && marker<std::int64_t(progress::layout::lines_per_script))progress_->state().mark_read(index_,std::uint32_t(marker));
        }
        last_line_=0;
    }
    index_=next;scene_=scene;
}
void SceneReadHistory::seen(std::string_view scene,std::int32_t line){
    if(line<0 || line>=std::int32_t(progress::layout::lines_per_script))throw Error("Native dialogue reading marker out of range");
    if(scene_!=scene)enter(scene);
    if(!progress_)throw Error("Missing native reading-history storage");
    progress_->state().mark_read(index_,std::uint32_t(line));last_line_=line;
    if(line%100==0)progress_->flush();
}
void SceneReadHistory::finish(){if(progress_)progress_->flush();}
std::int32_t SceneReadHistory::status(bool replay)const{
    if(last_line_==-1)return 0;
    if(!progress_)throw Error("Missing native reading-history storage");
    const auto line=std::int64_t(last_line_)+1;
    if(replay || (line>=0 && line<std::int64_t(progress::layout::lines_per_script) && progress_->state().read(index_,std::uint32_t(line))))return 1;
    return override_?2:0;
}
std::vector<std::uint8_t> SceneReadHistory::checkpoint()const{
    checkpoint::Writer out;out.string(scene_);out.u32(index_);out.i32(last_line_);out.i32(override_);return out.seal("LSRH");
}
void SceneReadHistory::restore(std::span<const std::uint8_t> bytes){
    auto in=checkpoint::Reader::open("LSRH",bytes);auto scene=in.string(240);const auto index=in.u32();const auto line=in.i32(),override=in.i32();
    if(index!=row(scene) || line<-1 || line>=std::int32_t(progress::layout::lines_per_script) || override<0)throw Error("Invalid saved reading-history position");
    in.finish();scene_=std::move(scene);index_=index;last_line_=line;override_=override;
}
}
