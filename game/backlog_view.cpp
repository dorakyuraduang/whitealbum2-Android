#include "backlog_view.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace wa2::game {
BacklogView::BacklogView(Loader images,Text text,Sound sound):loader_(std::move(images)),text_(std::move(text)),sound_(std::move(sound)){
    if(!loader_ || !text_)throw script::Error("Missing native backlog services");black_=std::make_shared<const image::Raster>(image::Raster{1,1,{0,0,0,255}});sprites_.reserve(2048);hits_.reserve(8);
}
void BacklogView::clear(){context_={};entries_.clear();rows_.clear();images_={};request_.reset();sprites_.clear();hits_.clear();focus_.reset();playing_.reset();command_.reset();phase_=BacklogPhase::idle;position_=image_next_=0;dragging_=false;}
std::string BacklogView::name(unsigned index)const{
    constexpr std::array<std::string_view,6> suffix{"0030","0031","0032","0033","9000","9100"};if(index>=suffix.size())throw script::Error("Unknown native backlog atlas");return "sys_"+std::to_string(context_.skin)+std::string(suffix[index])+".tga";
}
std::vector<BacklogView::Row> BacklogView::rows(std::size_t position,const std::deque<script::BacklogEntry>& entries,BacklogContext context)const{
    std::vector<Row> result;if(entries.empty())return result;const auto count=std::min<std::size_t>(context.novel?1:4,entries.size());result.reserve(count);
    for(std::size_t n=0;n<count;++n){Row row;row.entry=position+count-1-n;const auto& entry=entries.at(row.entry);text::TextStyle body;body.colour=text::speaker_colour(text::speaker_style(entry.speaker,entry.name));
        if(context.novel){body.columns=40;body.rows=15;}row.body=text_(entry.text,body);row.page=row.entry==0?context.current_page:0;
        if(row.page>=row.body.metrics().pages.size())throw script::Error("Native backlog page is outside its text");
        if(!context.novel && !entry.name.empty()){auto label=body;label.size=28;label.columns=36;label.rows=1;label.line_gap=0;label.spacing=2;label.colour={255,255,255};row.name=text_(entry.name,label);}result.push_back(std::move(row));
    }return result;
}
void BacklogView::prepare(BacklogContext context,const script::SceneBacklog& history){
    if(context.skin<0 || context.skin>9 || context.current_page>=4096 || history.entries().size()>script::SceneBacklog::capacity)throw script::Error("Invalid native backlog context");
    auto entries=history.entries();auto prepared=rows(0,entries,context);auto request=loader_("sys_"+std::to_string(context.skin)+"0030.tga");if(!request)throw script::Error("Missing native backlog image request");
    clear();context_=context;entries_=std::move(entries);rows_=std::move(prepared);request_=std::move(request);phase_=BacklogPhase::loading;
}
std::size_t BacklogView::maximum()const noexcept{const auto count=context_.novel?1u:4u;return entries_.size()>count?entries_.size()-count:0;}
void BacklogView::move(std::size_t position){
    if(position>maximum())throw script::Error("Native backlog scroll is out of range");if(position==position_)return;auto prepared=rows(position,entries_,context_);rows_=std::move(prepared);position_=position;focus_.reset();regions();if(sound_)sound_(audio::SystemCue::focus);
}
bool BacklogView::images(){
    constexpr unsigned widths[]{96,28,1280,64,1280,56},heights[]{640,84,40,672,56,72};
    for(unsigned issued=0;image_next_<images_.size();){if(!request_){if(issued==2)return false;request_=loader_(name(image_next_));++issued;if(!request_)throw script::Error("Missing native backlog image request");}
        auto pixels=request_->get();if(!pixels)return false;if(pixels->width<widths[image_next_] || pixels->height<heights[image_next_])throw script::Unsupported("Native backlog atlas dimensions are not supported");images_[image_next_++]=std::move(pixels);request_.reset();}
    return true;
}
void BacklogView::regions(){
    const float x=context_.novel?1228.f:1032.f;hits_={{0,x,32,32,48,position_<maximum()},{1,x,624,32,48,position_>0},{2,x+8,86,16,532,maximum()!=0},{3,1164,688,56,24}};
    if(!context_.novel)for(unsigned n=0;n<rows_.size();++n)hits_.push_back({4+n,236.f,32.f+n*160,28,28,!entries_[rows_[n].entry].voices.empty()});
}
void BacklogView::activate(unsigned id){
    if(id==0){if(position_<maximum())move(position_+1);return;}if(id==1){if(position_)move(position_-1);return;}if(id==2)return;
    if(id==3){phase_=BacklogPhase::closed;request_.reset();command_=BacklogCommand{BacklogCommand::Kind::close};if(sound_)sound_(audio::SystemCue::cancel);return;}
    if(id<4 || id-4>=rows_.size())throw script::Error("Unknown native backlog control");const auto entry=rows_[id-4].entry;
    if(entries_[entry].voices.empty()){if(sound_)sound_(audio::SystemCue::reject);return;}command_=BacklogCommand{BacklogCommand::Kind::replay,entry};if(sound_)sound_(audio::SystemCue::accept);
}
std::optional<BacklogCommand> BacklogView::take_command(){auto result=command_;command_.reset();return result;}
void BacklogView::update(BacklogInput input){
    const auto& selection=input.selection;if(!std::isfinite(selection.x) || !std::isfinite(selection.y) || std::abs(std::int64_t(selection.navigation))>1 || std::abs(std::int64_t(input.page_delta))>1 || std::abs(std::int64_t(input.scroll))>1)throw script::Error("Native backlog input is out of range");
    if(phase_==BacklogPhase::idle)throw script::Error("Native backlog is not prepared");if(phase_==BacklogPhase::closed || command_)return;
    if(input.cancel){activate(3);return;}if(phase_==BacklogPhase::loading){if(!images())return;phase_=BacklogPhase::active;regions();draw();return;}
    const auto step=input.page_delta*(context_.novel?1:4)+input.scroll-selection.navigation;
    if(step)move(std::size_t(std::clamp(std::int64_t(position_)+step,std::int64_t(0),std::int64_t(maximum()))));
    const auto previous=focus_;if(selection.pointer && !dragging_){focus_.reset();for(const auto& hit:hits_)if(selection.x>=hit.x && selection.x<hit.x+hit.width && selection.y>=hit.y && selection.y<hit.y+hit.height){focus_=hit.id;break;}}
    if(!input.held)dragging_=false;
    if(selection.pointer && input.held && (dragging_ || (selection.activate && focus_==2)) && maximum()){
        dragging_=true;const auto fraction=std::clamp((530.f-selection.y)/444.f,0.f,1.f);move(std::size_t(std::lround(fraction*maximum())));focus_=2;
    }else if(focus_ && (selection.confirm || (selection.pointer && selection.activate))){const auto hit=std::find_if(hits_.begin(),hits_.end(),[&](const auto& value){return value.id==*focus_;});if(hit->enabled)activate(*focus_);else if(sound_)sound_(audio::SystemCue::reject);}
    if(focus_ && focus_!=previous && sound_)sound_(audio::SystemCue::focus);draw();
}
void BacklogView::draw(){
    sprites_.clear();if(image_next_<images_.size() || phase_==BacklogPhase::closed)return;
    sprites_.push_back({black_,0,0,1280,720,0,0,1,1,255,255,255,128});sprites_.back().layer=0x1061;sprites_.back().order=0x41c;
    const auto add=[&](unsigned atlas,float x,float y,float width,float height,float sx,float sy,float sw,float sh,int layer,int order){sprites_.push_back({images_[atlas],x,y,width,height,sx,sy,sw,sh});sprites_.back().layer=layer;sprites_.back().order=order;};
    add(3,context_.novel?20.f:216.f,0,context_.novel?1240.f:848.f,672,0,0,64,672,0x1064,0x431);
    add(2,0,0,1280,40,0,0,1280,40,0x1065,0x432);add(4,0,664,1280,56,0,0,1280,56,0x1065,0x433);
    add(5,1164,688,56,24,0,focus_==3?24:0,56,24,0x1066,0x434);
    const float x=context_.novel?1228.f:1032.f;add(0,x,32,32,48,focus_==0?64:0,0,32,48,0x1065,0x42b);
    add(0,x,624,32,48,focus_==1?64:0,592,32,48,0x1065,0x42c);add(0,x,80,32,544,focus_==2?48:0,48,32,544,0x1065,0x42d);
    const float knob=530.f-(maximum()?float(position_)*444.f/float(maximum()):0);add(0,x+8,knob,16,88,dragging_?80:64,48,16,88,0x1066,0x42e);
    for(unsigned n=0;n<rows_.size();++n){const auto& row=rows_[n];const auto start=sprites_.size();const auto y=context_.novel?26.f:56.f+n*160;
        row.body.append(sprites_,row.page,std::numeric_limits<std::uint32_t>::max()/32,context_.novel?75.f:274.f,y,255,focus_==4+n?std::optional(text::speaker_colour(10)):std::nullopt);
        for(auto at=start;at<sprites_.size();++at){sprites_[at].layer=0x1065;sprites_[at].order=0x446+int(n);}
        if(context_.novel)continue;const auto name_start=sprites_.size();row.name.append(sprites_,0,100000,274,28.f+n*160);
        for(auto at=name_start;at<sprites_.size();++at){sprites_[at].layer=0x1065;sprites_[at].order=0x44a+int(n);}
        if(!entries_[row.entry].voices.empty())add(1,236,32.f+n*160,28,28,0,playing_==row.entry?0:28,28,28,0x1065,0x435+int(n));
    }
}
}
